#pragma once

#include <JuceHeader.h>
#include "DSP/FeedbackProcessor.h"
#include "DSP/ReverbProcessor.h"
#include "DSP/DatalossProcessor.h"
#include "DSP/TapeProcessor.h"

class NeoLiminalAudioProcessor : public juce::AudioProcessor
{
public:
    NeoLiminalAudioProcessor();
    ~NeoLiminalAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    std::atomic<bool> freezeRequest { false };

private:
    FeedbackProcessor   feedbackProcessor;
    ReverbProcessor     reverbProcessor;
    DatalossProcessor   datalossProcessor;
    TapeProcessor       tapeProcessor;

    // Freeze — audio thread only
    bool  freezeActive     = false;
    float freezeCrossfade  = 0.0f;
    float freezeFadeRate   = 0.0f;
    int   freezeBufLen     = 0;
    int   freezeWritePos    = 0;
    int   freezeLoopStart   = 0;
    int   freezeLoopLen     = 0; // 2s loop; total buffer is 4s so write never catches read
    int   freezeLoopFade    = 0; // = loopLen/2 bidirectional crossfade
    int   freezeReadOffset  = 0;
    std::array<std::vector<float>, 2> freezeBuf;      // 4s circular, always recording
    std::array<std::vector<float>, 2> snapshotBuf;   // 2s linear snapshot, filled at activation

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeoLiminalAudioProcessor)
};