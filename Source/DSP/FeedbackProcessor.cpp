#include "FeedbackProcessor.h"
#include "DSPUtils.h"

void FeedbackProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        delayBuffer[ch].assign(kMaxDelaySamples, 0.0f);
        writePos[ch] = 0;
    }

    juce::dsp::ProcessSpec monoSpec{ spec.sampleRate, spec.maximumBlockSize, 1 };
    for (auto& f : lpFilter)
    {
        f.prepare(monoSpec);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 4000.0f);
    }

    const double smoothMs = 20.0;
    smoothDelayMs.reset(sampleRate, smoothMs * 0.001);
    smoothFeedback.reset(sampleRate, smoothMs * 0.001);
    smoothWetMix.reset(sampleRate, smoothMs * 0.001);
    smoothDryMix.reset(sampleRate, smoothMs * 0.001);

    smoothDelayMs.setCurrentAndTargetValue(10.0f);
    smoothFeedback.setCurrentAndTargetValue(0.0f);
    smoothWetMix.setCurrentAndTargetValue(0.0f);
    smoothDryMix.setCurrentAndTargetValue(1.0f);

    smoothPitchRatio.reset(sampleRate, 0.050);
    smoothPitchRatio.setCurrentAndTargetValue(1.0f);

    hpAlpha    = std::exp(-(float)(2.0 * juce::MathConstants<double>::pi * 20.0 / sampleRate));
    hpState[0] = hpState[1] = 0.0f;
    pitchOffset[0]  = pitchOffset[1]  = 0.0f;
    pitchOffsetB[0] = pitchOffsetB[1] = 0.0f;
    pitchCrossfade  = 0.0f;
    pitchCrossfading = false;
    pitchExiting    = false;
    pitchExitFade   = 0.0f;
}

void FeedbackProcessor::reset()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        std::fill(delayBuffer[ch].begin(), delayBuffer[ch].end(), 0.0f);
        writePos[ch] = 0;
    }

    for (auto& f : lpFilter)
        f.reset();

    smoothDelayMs.setCurrentAndTargetValue(10.0f);
    smoothFeedback.setCurrentAndTargetValue(0.0f);
    smoothWetMix.setCurrentAndTargetValue(0.0f);
    smoothDryMix.setCurrentAndTargetValue(1.0f);
    smoothPitchRatio.setCurrentAndTargetValue(1.0f);
    hpState[0] = hpState[1] = 0.0f;
    pitchOffset[0]  = pitchOffset[1]  = 0.0f;
    pitchOffsetB[0] = pitchOffsetB[1] = 0.0f;
    pitchCrossfade  = 0.0f;
    pitchCrossfading = false;
    pitchExiting    = false;
    pitchExitFade   = 0.0f;
}

float FeedbackProcessor::readDelay(size_t channel, float delaySamples)
{
    const auto& buf     = delayBuffer[channel];
    const int   bufSize = (int)buf.size();
    // Add 2*bufSize before fmod so the argument is always positive even when
    // delaySamples > bufSize (can happen during pitch crossfade spiral).
    const float readF   = std::fmod((float)writePos[channel] - delaySamples + 2.0f * (float)bufSize, (float)bufSize);
    const int   i1      = (int)readF;
    const float frac    = readF - (float)i1;

    const int i0 = (i1 - 1 + bufSize) % bufSize;
    const int i2 = (i1 + 1) % bufSize;
    const int i3 = (i1 + 2) % bufSize;

    const float y0 = buf[(size_t)i0];
    const float y1 = buf[(size_t)i1];
    const float y2 = buf[(size_t)i2];
    const float y3 = buf[(size_t)i3];

    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void FeedbackProcessor::process(juce::AudioBuffer<float>& buffer, float amount)
{
    if (amount < 0.001f)
    {
        smoothDelayMs.setTargetValue(10.0f);
        smoothFeedback.setTargetValue(0.0f);
        smoothWetMix.setTargetValue(0.0f);
        smoothDryMix.setTargetValue(1.0f);
        smoothPitchRatio.setTargetValue(1.0f);

        if (!smoothWetMix.isSmoothing() && !pitchExiting)
            return;
    }
    else
    {
        // Delay: 100ms → 500ms over 0–50%, fixed at 500ms from 50–100%
        smoothDelayMs.setTargetValue(amount <= 0.5f ? 100.0f + 800.0f * amount : 500.0f);
        // Feedback: 0.35 → 0.88 over 0–50%, fixed at 0.88 from 50–100%
        smoothFeedback.setTargetValue(amount <= 0.5f ? 0.35f + 1.06f * amount : 0.88f);
        const float wet = 0.40f + 0.60f * amount;
        smoothWetMix.setTargetValue(wet);
        smoothDryMix.setTargetValue(1.0f - wet);

        // Pitch slowdown: no shift at 50%, -1 octave at 100%
        const float pitchAmt = juce::jmax(0.0f, juce::jmin((amount - 0.5f) / 0.50f, 1.0f));
        smoothPitchRatio.setTargetValue(pitchAmt > 0.001f ? std::pow(0.5f, pitchAmt) : 1.0f);
    }

    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float currentDelayMs    = smoothDelayMs.getNextValue();
        const float currentFeedback   = smoothFeedback.getNextValue();
        const float currentWet        = smoothWetMix.getNextValue();
        const float currentDry        = smoothDryMix.getNextValue();
        const float currentPitchRatio = smoothPitchRatio.getNextValue();

        const float delayL = juce::jmin(
            static_cast<float>(currentDelayMs * 0.001 * sampleRate),
            static_cast<float>(kMaxDelaySamples - 2));
        const float delayR = juce::jmin(
            static_cast<float>((currentDelayMs + 15.0f) * 0.001 * sampleRate),
            static_cast<float>(kMaxDelaySamples - 2));
        const float channelDelay[2] = { delayL, delayR };

        float inputSamples[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < numChannels; ++ch)
            inputSamples[ch] = buffer.getReadPointer(ch)[sample];

        // Non-pitched delay read (used for feedback — stable regardless of knob movement)
        float normalDelayed[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < numChannels; ++ch)
            normalDelayed[ch] = readDelay((size_t)ch, channelDelay[ch]);

        // --- Pitch spiral: dual read heads with cosine crossfade ---
        const bool inPitchMode = (currentPitchRatio < 0.999f);

        // Detect transition out of pitch mode — trigger exit crossfade to avoid click
        if (!inPitchMode && !pitchExiting && pitchOffset[0] > channelDelay[0] + 1.0f)
        {
            pitchExiting  = true;
            pitchExitFade = 0.0f;
        }
        if (inPitchMode)
        {
            pitchExiting  = false;
            pitchExitFade = 0.0f;
        }

        float delayed[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const size_t sch = (size_t)ch;

            if (!inPitchMode && !pitchExiting)
            {
                pitchOffset[ch] = channelDelay[ch];
                delayed[ch]     = normalDelayed[ch];
            }
            else if (pitchExiting)
            {
                // Cosine crossfade from current (drifted) read head to normal delay
                const float blend = 0.5f * (1.0f - std::cos(pitchExitFade * juce::MathConstants<float>::pi));
                delayed[ch] = readDelay(sch, pitchOffset[ch]) * (1.0f - blend)
                            + normalDelayed[ch] * blend;
            }
            else
            {
                // Active pitch spiral
                const float drift = 1.0f - currentPitchRatio;
                pitchOffset[ch] += drift;
                if (pitchCrossfading) pitchOffsetB[ch] += drift;

                const float sampA = readDelay(sch, pitchOffset[ch]);
                if (pitchCrossfading)
                {
                    const float blend = 0.5f * (1.0f - std::cos(
                        pitchCrossfade * juce::MathConstants<float>::pi));
                    delayed[ch] = sampA * (1.0f - blend)
                                + readDelay(sch, pitchOffsetB[ch]) * blend;
                }
                else
                {
                    delayed[ch] = sampA;
                }
            }
        }

        // Advance exit crossfade (200ms)
        if (pitchExiting)
        {
            const float exitRate = 1.0f / (0.200f * (float)sampleRate);
            pitchExitFade = juce::jmin(1.0f, pitchExitFade + exitRate);
            if (pitchExitFade >= 1.0f)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    pitchOffset[ch] = channelDelay[ch];
                pitchExiting = false;
            }
        }

        // Pitch crossfade state update
        if (inPitchMode)
        {
            const float triggerAt     = static_cast<float>(kMaxDelaySamples) * 0.70f;
            const float crossfadeRate = 1.0f / (6.0f * static_cast<float>(sampleRate));

            if (!pitchCrossfading && pitchOffset[0] >= triggerAt)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    pitchOffsetB[ch] = channelDelay[ch];
                pitchCrossfading = true;
                pitchCrossfade   = 0.0f;
            }

            if (pitchCrossfading)
            {
                pitchCrossfade = juce::jmin(1.0f, pitchCrossfade + crossfadeRate);
                if (pitchCrossfade >= 1.0f)
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        pitchOffset[ch] = pitchOffsetB[ch];
                    pitchCrossfading = false;
                    pitchCrossfade   = 0.0f;
                }
            }
        }
        else if (!pitchExiting)
        {
            pitchCrossfading = false;
            pitchCrossfade   = 0.0f;
        }

        // Write delay buffer (always uses normalDelayed for feedback — no pitch compounding)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const size_t sch = (size_t)ch;
            // HP at 20Hz (LP subtraction) then LP at 4kHz — prevents DC buildup in loop
            hpState[sch] = (1.0f - hpAlpha) * normalDelayed[ch] + hpAlpha * hpState[sch];
            const float hpDelayed               = normalDelayed[ch] - hpState[sch];
            const float filteredFb              = lpFilter[sch].processSample(hpDelayed);
            delayBuffer[sch][writePos[sch]]     = inputSamples[ch] + filteredFb * currentFeedback;
            writePos[sch]                       = (writePos[sch] + 1) % kMaxDelaySamples;
        }

        // Output
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[sample] = currentDry * inputSamples[ch] + currentWet * delayed[ch];
    }

    DSPUtils::applySoftClip(buffer);
}
