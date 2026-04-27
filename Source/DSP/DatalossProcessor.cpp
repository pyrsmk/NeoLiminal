#include "DatalossProcessor.h"
#include "DSPUtils.h"

void DatalossProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate      = spec.sampleRate;
    envelope        = 0.0f;
    gainSmoothed    = 1.0f;
    compDropTarget  = 1.0f;
    holdCounter     = 0;
    compActive      = false;
    compHolding     = false;
    compReleasing   = false;
    cooldownCounter = 0;
    dcBlockX[0] = dcBlockX[1] = 0.0f;
    dcBlockY[0] = dcBlockY[1] = 0.0f;
    satCompGain  = 1.0f;
    smoothAmount.reset(sampleRate, 0.3);
    smoothAmount.setCurrentAndTargetValue(0.0f);

    attackCoeff      = std::exp(-1.0f / (float)(1.0   * 0.001 * sampleRate));
    releaseCoeff     = std::exp(-1.0f / (float)(250.0 * 0.001 * sampleRate));
    gainAttackCoeff  = std::exp(-1.0f / (float)(5.0   * 0.001 * sampleRate));
    gainReleaseCoeff = std::exp(-1.0f / (float)(10.0  * 0.001 * sampleRate));

    inputRmsFollower      = 0.0f;
    inputRmsFollowerCoeff = std::exp(-1.0f / (float)(200.0 * 0.001 * sampleRate));
    satPreGain            = 1.0f;
}

void DatalossProcessor::reset()
{
    envelope        = 0.0f;
    gainSmoothed    = 1.0f;
    compDropTarget  = 1.0f;
    holdCounter     = 0;
    compActive      = false;
    compHolding     = false;
    compReleasing   = false;
    cooldownCounter = 0;
    dcBlockX[0] = dcBlockX[1] = 0.0f;
    dcBlockY[0] = dcBlockY[1] = 0.0f;
    satCompGain       = 1.0f;
    inputRmsFollower  = 0.0f;
    satPreGain        = 1.0f;
    smoothAmount.setCurrentAndTargetValue(0.0f);
}

void DatalossProcessor::process(juce::AudioBuffer<float>& buffer, float amount)
{
    smoothAmount.setTargetValue(amount);

    if (amount < 0.01f && !smoothAmount.isSmoothing())
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const float dcAlpha   = 0.9995f;
    const int satChannels = juce::jmin(numChannels, 2);

    // Pre-compute per-sample smoothed values (two passes below need the same values)
    juce::AudioBuffer<float> smBuf(1, numSamples);
    float* smValues = smBuf.getWritePointer(0);
    for (int s = 0; s < numSamples; ++s)
        smValues[s] = smoothAmount.getNextValue();

    // -----------------------------------------------------------------------
    // Input RMS follower → pre-normalisation (keeps saturation level-consistent)
    // -----------------------------------------------------------------------
    {
        float blockSumSq = 0.0f;
        for (int ch = 0; ch < satChannels; ++ch)
        {
            const float* rd = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
                blockSumSq += rd[s] * rd[s];
        }
        inputRmsFollower = inputRmsFollowerCoeff * inputRmsFollower
                         + (1.0f - inputRmsFollowerCoeff)
                           * (blockSumSq / (float)(numSamples * satChannels));
    }
    const float inputRms     = std::sqrt(juce::jmax(0.0f, inputRmsFollower));
    const float kRefRms      = 0.126f; // -18dBFS reference level
    const float targetPreGain = (inputRms > 1e-4f)
                               ? juce::jlimit(0.1f, 1.0f, kRefRms / inputRms)
                               : 1.0f;
    const float pgStart = satPreGain;
    const float pgDelta = (targetPreGain - pgStart) / (float)numSamples;
    satPreGain = targetPreGain;

    for (int ch = 0; ch < satChannels; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            data[s] *= pgStart + pgDelta * (float)s;
    }

    // -----------------------------------------------------------------------
    // Pass 1 — Tube saturation (0–50%) with block-level RMS compensation
    // -----------------------------------------------------------------------
    float inputSumSq = 0.0f;
    float satSumSq   = 0.0f;

    for (int s = 0; s < numSamples; ++s)
    {
        const float tubeT = juce::jmin(smValues[s] / 0.5f, 1.0f);

        for (int ch = 0; ch < satChannels; ++ch)
        {
            const float x = buffer.getReadPointer(ch)[s];
            inputSumSq += x * x;

            if (tubeT > 0.001f)
            {
                const float drive = 1.0f + tubeT * 14.0f;
                const float bias  = tubeT * 0.15f;
                const float sat   = std::tanh(x * drive + bias);
                const float y     = sat - dcBlockX[ch] + dcAlpha * dcBlockY[ch];
                dcBlockX[ch] = sat;
                dcBlockY[ch] = y;
                const float out = x + (y - x) * (tubeT * 0.92f);
                buffer.getWritePointer(ch)[s] = out;
                satSumSq += out * out;
            }
            else
            {
                satSumSq += x * x;
            }
        }
    }

    // Compute target gain, then ramp linearly from previous block's gain to new one.
    // This eliminates the step discontinuity at block boundaries (source of crackles).
    const float targetGain = (inputSumSq > 1e-6f && satSumSq > 1e-6f)
                           ? juce::jlimit(0.1f, 4.0f, std::sqrt(inputSumSq / satSumSq))
                           : satCompGain;
    const float gainStart = satCompGain;
    const float gainDelta = (targetGain - gainStart) / (float)numSamples;
    for (int ch = 0; ch < satChannels; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            data[s] *= gainStart + gainDelta * (float)s;
    }
    satCompGain = targetGain;

    // Restore original level: undo pre-normalisation
    for (int ch = 0; ch < satChannels; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            const float pg = pgStart + pgDelta * (float)s;
            data[s] /= pg;
        }
    }

    // -----------------------------------------------------------------------
    // Pass 2 — Compressor (51–100%)
    // State machine: COOLDOWN → ATTACK → HOLD → RELEASE → COOLDOWN
    // -----------------------------------------------------------------------
    for (int s = 0; s < numSamples; ++s)
    {
        const float sm = smValues[s];

        if (sm > 0.5f)
        {
            const float compT = juce::jmax(0.0f, (sm - 0.5f) / 0.5f);

            // Envelope follower
            float detectedLevel = 0.0f;
            for (int ch = 0; ch < satChannels; ++ch)
                detectedLevel = juce::jmax(detectedLevel,
                                           std::abs(buffer.getReadPointer(ch)[s]));

            if (detectedLevel > envelope)
                envelope = attackCoeff  * envelope + (1.0f - attackCoeff)  * detectedLevel;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * detectedLevel;

            const float thresholdDB  = -24.0f - 8.0f * compT; // -24dBFS → -32dBFS
            const float thresholdLin = juce::Decibels::decibelsToGain(thresholdDB);

            // --- COOLDOWN ---
            if (cooldownCounter > 0)
            {
                --cooldownCounter;
            }
            // --- IDLE: trigger on peak ---
            else if (!compActive && envelope > thresholdLin)
            {
                // Drop depth: 1–6dB at 51%, 1–12dB at 100%
                const float minDropDB = -1.0f;
                const float maxDropDB = -(4.0f + compT * 8.0f); // -4dB → -12dB
                const float grDB      = minDropDB + random.nextFloat() * (maxDropDB - minDropDB);
                compDropTarget = juce::Decibels::decibelsToGain(grDB);

                compActive    = true;
                compHolding   = false;
                compReleasing = false;
            }

            // --- ATTACK: gain drops fast to target ---
            if (compActive && !compHolding && !compReleasing)
            {
                gainSmoothed = gainAttackCoeff  * gainSmoothed
                             + (1.0f - gainAttackCoeff) * compDropTarget;

                if (gainSmoothed <= compDropTarget + 0.005f)
                {
                    gainSmoothed = compDropTarget;
                    compHolding  = true;
                    const float minH = 0.010f - compT * 0.005f; // 10ms → 5ms
                    const float maxH = 0.750f - compT * 0.700f; // 750ms → 50ms
                    holdCounter = (int)((minH + random.nextFloat() * (maxH - minH))
                                       * (float)sampleRate);
                }
            }
            // --- HOLD: stay compressed for random duration ---
            else if (compActive && compHolding)
            {
                gainSmoothed = compDropTarget;
                if (--holdCounter <= 0)
                {
                    compHolding   = false;
                    compReleasing = true;
                }
            }
            // --- RELEASE: gain returns fast to unity ---
            else if (compActive && compReleasing)
            {
                gainSmoothed = gainReleaseCoeff * gainSmoothed
                             + (1.0f - gainReleaseCoeff) * 1.0f;

                if (gainSmoothed >= 0.99f)
                {
                    gainSmoothed  = 1.0f;
                    compActive    = false;
                    compReleasing = false;
                    const float minC = 0.010f - compT * 0.005f; // 10ms → 5ms
                    const float maxC = 0.750f - compT * 0.700f; // 750ms → 50ms
                    cooldownCounter = (int)(
                        (minC + random.nextFloat() * (maxC - minC))
                        * (float)sampleRate);
                }
            }

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer(ch)[s] *= gainSmoothed;
        }
        else
        {
            if (gainSmoothed < 0.99f)
                gainSmoothed = gainReleaseCoeff * gainSmoothed
                             + (1.0f - gainReleaseCoeff) * 1.0f;
        }
    }

    DSPUtils::applySoftClip(buffer);
}
