#include "PluginProcessor.h"
#include "PluginEditor.h"

NeoLiminalAudioProcessor::NeoLiminalAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input",   juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

NeoLiminalAudioProcessor::~NeoLiminalAudioProcessor() {}

const juce::String NeoLiminalAudioProcessor::getName() const { return "NeoLiminal"; }

bool NeoLiminalAudioProcessor::acceptsMidi()  const { return false; }
bool NeoLiminalAudioProcessor::producesMidi() const { return false; }
bool NeoLiminalAudioProcessor::isMidiEffect() const { return false; }
double NeoLiminalAudioProcessor::getTailLengthSeconds() const { return 2.0; }

int NeoLiminalAudioProcessor::getNumPrograms()                              { return 1; }
int NeoLiminalAudioProcessor::getCurrentProgram()                           { return 0; }
void NeoLiminalAudioProcessor::setCurrentProgram(int)                       {}
const juce::String NeoLiminalAudioProcessor::getProgramName(int)            { return {}; }
void NeoLiminalAudioProcessor::changeProgramName(int, const juce::String&)  {}

bool NeoLiminalAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* NeoLiminalAudioProcessor::createEditor()
{
    return new NeoLiminalAudioProcessorEditor(*this);
}

bool NeoLiminalAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void NeoLiminalAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels      = static_cast<juce::uint32>(getTotalNumOutputChannels());

    feedbackProcessor.prepare(spec);
    reverbProcessor.prepare(spec);
    datalossProcessor.prepare(spec);
    tapeProcessor.prepare(spec);

    // 4s circular buffer (always recording) + 2s snapshot (filled at activation).
    // Bidirectional fade = loopLen/2 → wrap transition between adjacent samples.
    freezeBufLen   = (int)(8.0 * sampleRate);
    freezeLoopLen  = (int)(4.0 * sampleRate);
    freezeLoopFade = freezeLoopLen / 2;
    freezeWritePos   = 0;
    freezeLoopStart  = 0;
    freezeReadOffset = 0;
    freezeActive     = false;
    freezeCrossfade  = 0.0f;
    freezeFadeRate   = 1.0f / (0.750f * (float)sampleRate);
    for (auto& ch : freezeBuf)
        ch.assign((size_t)freezeBufLen, 0.0f);
    for (auto& ch : snapshotBuf)
        ch.assign((size_t)freezeLoopLen, 0.0f);
}

void NeoLiminalAudioProcessor::releaseResources()
{
    feedbackProcessor.reset();
    reverbProcessor.reset();
    datalossProcessor.reset();
    tapeProcessor.reset();
}

void NeoLiminalAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const float feedbackAmount    = apvts.getRawParameterValue("feedback")->load()    / 100.0f;
    const float reverbAmount      = apvts.getRawParameterValue("reverb")->load()      / 100.0f;
    const float datalossAmount    = apvts.getRawParameterValue("dataloss")->load()    / 100.0f;
    const float tapeAmount        = apvts.getRawParameterValue("tape")->load()        / 100.0f;

    feedbackProcessor.process(buffer,    feedbackAmount);
    reverbProcessor.process(buffer,      reverbAmount);
    datalossProcessor.process(buffer,    datalossAmount);
    tapeProcessor.process(buffer,        tapeAmount);

    // --- Freeze ---
    const bool wantFreeze = freezeRequest.load();
    const int  numCh      = juce::jmin(buffer.getNumChannels(), 2);
    const int  numSamples = buffer.getNumSamples();

    for (int s = 0; s < numSamples; ++s)
    {
        // Always write live audio — buffer is 4s, loop reads 2s, write never catches read
        for (int ch = 0; ch < numCh; ++ch)
            freezeBuf[(size_t)ch][(size_t)freezeWritePos] = buffer.getReadPointer(ch)[s];
        if (++freezeWritePos >= freezeBufLen)
            freezeWritePos = 0;

        // Activate freeze: copy last 2s of live audio into snapshotBuf (static, won't change)
        if (wantFreeze && !freezeActive)
        {
            freezeActive    = true;
            freezeLoopStart = (freezeWritePos - freezeLoopLen + freezeBufLen) % freezeBufLen;
            freezeReadOffset = 0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const int part1 = juce::jmin(freezeLoopLen, freezeBufLen - freezeLoopStart);
                std::copy(freezeBuf[(size_t)ch].begin() + freezeLoopStart,
                          freezeBuf[(size_t)ch].begin() + freezeLoopStart + part1,
                          snapshotBuf[(size_t)ch].begin());
                if (part1 < freezeLoopLen)
                    std::copy(freezeBuf[(size_t)ch].begin(),
                              freezeBuf[(size_t)ch].begin() + (freezeLoopLen - part1),
                              snapshotBuf[(size_t)ch].begin() + part1);
            }
        }

        // Read from snapshotBuf (static): bidirectional crossfade, fade = loopLen/2.
        // Near start (p < fade): blend with second half (p + N/2).
        // Near end  (p >= N-fade): blend with first half (p - N/2).
        // Wrap transition: s[N/2-1] → s[N/2] (adjacent samples) → no click.
        float frozen[2] = { 0.0f, 0.0f };
        if (freezeActive)
        {
            const int   p    = freezeReadOffset;
            const int   N    = freezeLoopLen;
            const int   fade = freezeLoopFade;

            for (int ch = 0; ch < numCh; ++ch)
                frozen[ch] = snapshotBuf[(size_t)ch][(size_t)p];

            if (p < fade)
            {
                const float t    = (float)p / (float)fade;
                const int   pAlt = N - fade + p;
                for (int ch = 0; ch < numCh; ++ch)
                    frozen[ch] = frozen[ch] * t
                               + snapshotBuf[(size_t)ch][(size_t)pAlt] * (1.0f - t);
            }
            else if (p >= N - fade)
            {
                const float t    = (float)(p - (N - fade)) / (float)fade;
                const int   pAlt = p - (N - fade);
                for (int ch = 0; ch < numCh; ++ch)
                    frozen[ch] = frozen[ch] * (1.0f - t)
                               + snapshotBuf[(size_t)ch][(size_t)pAlt] * t;
            }

            if (++freezeReadOffset >= N)
                freezeReadOffset = 0;
        }

        // Ramp crossfade
        if (wantFreeze)
            freezeCrossfade = juce::jmin(1.0f, freezeCrossfade + freezeFadeRate);
        else
            freezeCrossfade = juce::jmax(0.0f, freezeCrossfade - freezeFadeRate);

        if (!wantFreeze && freezeActive && freezeCrossfade <= 0.0f)
            freezeActive = false;

        // Mix output
        const float cf = freezeCrossfade;
        if (cf > 0.0f)
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer(ch)[s] = buffer.getReadPointer(ch)[s] * (1.0f - cf)
                                              + frozen[ch] * cf;
    }
}

void NeoLiminalAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NeoLiminalAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState::ParameterLayout NeoLiminalAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "feedback",    1 }, "Feedback",    0.0f, 100.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "reverb",      1 }, "Reverb",      0.0f, 100.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "dataloss",    1 }, "Dataloss",    0.0f, 100.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "tape",        1 }, "Tape",        0.0f, 100.0f, 0.0f));

    return layout;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeoLiminalAudioProcessor();
}