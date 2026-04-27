#include "TapeProcessor.h"
#include "DSPUtils.h"

void TapeProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;

    for (size_t ch = 0; ch < 2; ++ch)
    {
        wowBuffer[ch].assign(kWowBufSize, 0.0f);
        wowWritePos[ch] = 0;
    }

    vibratoPhase = 0.0;
    wowPhase     = 0.0;
    flutterPhase = 0.0;

    lpState[0] = lpState[1] = 0.0f;
    hpState[0] = hpState[1] = 0.0f;
    wowNoiseLP     = 0.0f;
    flutterNoiseLP = 0.0f;

    // Brown noise: 200Hz LP gives warm tape-hiss character
    brownAlpha     = std::exp(-2.0f * juce::MathConstants<float>::pi * 200.0f / (float)sampleRate);
    brownNormScale = std::sqrt((1.0f - brownAlpha) / (1.0f + brownAlpha));
    brownNoiseState[0] = brownNoiseState[1] = 0.0f;

    flutterBurstEnv     = 0.0f;
    flutterBurstCounter = (int)(sampleRate * 2.0); // first burst after ~2s

    smoothAmount.reset(sampleRate, 0.100);
    smoothAmount.setCurrentAndTargetValue(0.0f);

    smoothCassT.reset(sampleRate, 0.300);
    smoothCassT.setCurrentAndTargetValue(0.0f);
}

void TapeProcessor::reset()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        std::fill(wowBuffer[ch].begin(), wowBuffer[ch].end(), 0.0f);
        wowWritePos[ch] = 0;
    }

    vibratoPhase = 0.0;
    wowPhase     = 0.0;
    flutterPhase = 0.0;
    lpState[0] = lpState[1] = 0.0f;
    hpState[0] = hpState[1] = 0.0f;
    wowNoiseLP     = 0.0f;
    flutterNoiseLP = 0.0f;
    brownNoiseState[0] = brownNoiseState[1] = 0.0f;
    flutterBurstEnv     = 0.0f;
    flutterBurstPeak    = 0.0f;
    flutterBurstRising  = false;
    flutterBurstCounter = (int)(sampleRate * 2.0);

    smoothAmount.setCurrentAndTargetValue(0.0f);
    smoothCassT.setCurrentAndTargetValue(0.0f);
}

float TapeProcessor::readInterpolated(const std::vector<float>& buf, size_t wPos, float delaySamples)
{
    const int   bufSize  = (int)buf.size();
    const float fBufSize = (float)bufSize;
    float readF = (float)(int)wPos - delaySamples;
    while (readF <  0.0f)      readF += fBufSize;
    while (readF >= fBufSize)  readF -= fBufSize;

    const int   i1   = (int)readF;
    const float frac = readF - (float)i1;

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

void TapeProcessor::process(juce::AudioBuffer<float>& buffer, float amount)
{
    smoothAmount.setTargetValue(amount);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin(buffer.getNumChannels(), 2);

    if (amount < 0.001f && !smoothAmount.isSmoothing())
    {
        for (int s = 0; s < numSamples; ++s)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const size_t sch = static_cast<size_t>(ch);
                const float x = buffer.getReadPointer(ch)[s];
                wowBuffer[sch][wowWritePos[sch]] = x;
                wowWritePos[sch] = (wowWritePos[sch] + 1) % kWowBufSize;
                lpState[sch] = x;
            }
        }
        return;
    }

    smoothCassT.setTargetValue(juce::jmax(0.0f, (amount - 0.5f) / 0.5f));

    const float alphaHigh = std::exp(-(float)(2.0 * juce::MathConstants<double>::pi * 20000.0 / sampleRate));
    const float alphaLow  = std::exp(-(float)(2.0 * juce::MathConstants<double>::pi *  3000.0 / sampleRate));

    // HP filter: 20Hz at 0% → 100Hz at 100% (light bass cut via LP subtraction)
    const float alphaHpMin = std::exp(-(float)(2.0 * juce::MathConstants<double>::pi *  20.0 / sampleRate));
    const float alphaHpMax = std::exp(-(float)(2.0 * juce::MathConstants<double>::pi * 100.0 / sampleRate));

    const float vibratoMax  = (float)(0.75 * 0.001 * sampleRate);
    const float cassWowMax  = (float)(1.5  * 0.001 * sampleRate);
    // Base flutter depth (continuous, subtle)
    const float cassFlutMax = (float)(0.20 * 0.001 * sampleRate);
    // Burst flutter depth (occasional, large)
    const float cassFlutBurstMax = (float)(2.0 * 0.001 * sampleRate);

    // Burst attack (~50ms) and decay (~400ms) time constants
    const float burstAttack = std::exp(-1.0f / (float)(0.050 * sampleRate));
    const float burstDecay  = std::exp(-1.0f / (float)(0.400 * sampleRate));

    static constexpr double kTwoPi       = juce::MathConstants<double>::twoPi;
    static constexpr double kVibratoFreq = 2.0;
    static constexpr double kWowFreq     = 0.35;
    static constexpr double kFlutterFreq = 8.0;
    const double invSR = 1.0 / sampleRate;

    // Noise LP filter coefficients: wow noise ~3Hz BW, flutter noise ~20Hz BW
    const float wowNoiseAlpha     = std::exp(-(float)(kTwoPi * 3.0  / sampleRate));
    const float flutterNoiseAlpha = std::exp(-(float)(kTwoPi * 20.0 / sampleRate));

    // When knob moves, recompute burst interval for the new position (if no burst active)
    if (smoothCassT.isSmoothing() && flutterBurstEnv < 0.01f)
    {
        const float cassT = smoothCassT.getCurrentValue();
        const double maxInterval = 20.0 - 15.0 * (double)cassT;
        flutterBurstCounter = (int)((2.0 + random.nextDouble() * (maxInterval - 2.0)) * sampleRate);
    }

    for (int s = 0; s < numSamples; ++s)
    {
        const float sm_s      = smoothAmount.getNextValue();
        const float sCassT_s  = smoothCassT.getNextValue();
        const float vibratoT_s = juce::jmin(sm_s / 0.5f, 1.0f);

        // Random flutter burst: triggers every 2–6 seconds, random peak strength 30–100%
        if (--flutterBurstCounter <= 0)
        {
            flutterBurstPeak   = 0.3f + random.nextFloat() * 0.7f;
            flutterBurstRising = true;
            const double maxInterval = 20.0 - 15.0 * (double)sCassT_s; // 20s at 51%, 5s at 100%
            const double intervalSec = 2.0 + random.nextDouble() * (maxInterval - 2.0);
            flutterBurstCounter = (int)(intervalSec * sampleRate);
        }
        if (flutterBurstRising)
        {
            flutterBurstEnv += (flutterBurstPeak - flutterBurstEnv) * (1.0f - burstAttack);
            if (flutterBurstEnv >= flutterBurstPeak * 0.99f)
                flutterBurstRising = false;
        }
        else
        {
            flutterBurstEnv *= burstDecay;
        }

        const float vDepth = vibratoT_s * vibratoMax;
        const float wDepth = sCassT_s   * cassWowMax;
        const float fDepth = sCassT_s * (cassFlutMax + flutterBurstEnv * cassFlutBurstMax);

        const float centreSamp = vDepth + wDepth + fDepth + 2.0f;
        const float maxMod     = centreSamp - 1.0f;

        const float alpha     = alphaHigh + (alphaLow - alphaHigh) * sm_s;
        const float alphaGain = 1.0f - alpha;

        vibratoPhase = std::fmod(vibratoPhase + kVibratoFreq * invSR * kTwoPi, kTwoPi);
        wowPhase     = std::fmod(wowPhase     + kWowFreq     * invSR * kTwoPi, kTwoPi);
        flutterPhase = std::fmod(flutterPhase + kFlutterFreq * invSR * kTwoPi, kTwoPi);

        // Band-limited noise: white noise passed through 1-pole LP
        // mixed with sinusoid (70% sine dominant rotation + 30% random variation)
        const float wowRaw     = random.nextFloat() * 2.0f - 1.0f;
        wowNoiseLP     = (1.0f - wowNoiseAlpha)     * wowRaw     + wowNoiseAlpha     * wowNoiseLP;
        const float flutterRaw = random.nextFloat() * 2.0f - 1.0f;
        flutterNoiseLP = (1.0f - flutterNoiseAlpha) * flutterRaw + flutterNoiseAlpha * flutterNoiseLP;

        const float vibratoMod  = (float)std::sin(vibratoPhase) * vDepth;
        const float cassWowMod  = ((float)std::sin(wowPhase) * 0.7f + wowNoiseLP * 0.3f) * wDepth;
        const float cassFlutMod = ((float)std::sin(flutterPhase) * 0.7f + flutterNoiseLP * 0.3f) * fDepth;

        const float readDelay = centreSamp
                              + juce::jlimit(-maxMod, maxMod, vibratoMod + cassWowMod + cassFlutMod);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* const data = buffer.getWritePointer(ch);
            float x = data[s];
            const size_t sch = static_cast<size_t>(ch);

            wowBuffer[sch][wowWritePos[sch]] = x;
            x = readInterpolated(wowBuffer[sch], wowWritePos[sch], readDelay);
            wowWritePos[sch] = (wowWritePos[sch] + 1) % kWowBufSize;

            // HP filter (LP subtraction): 20Hz→100Hz over 0–100%
            const float alphaHp = alphaHpMin + (alphaHpMax - alphaHpMin) * sm_s;
            hpState[sch] = (1.0f - alphaHp) * x + alphaHp * hpState[sch];
            x = x - hpState[sch];

            // LP filter: 20kHz→3kHz over 0–100%
            lpState[sch] = alphaGain * x + alpha * lpState[sch];
            x = lpState[sch];

            data[s] = x;
        }

        // Brown noise: -70dBFS at 1% knob → -40dBFS at 100% knob
        const float brownLevel = 0.005623f * std::pow(sm_s, 0.625f);
        if (brownLevel > 0.0f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float white = random.nextFloat() * 2.0f - 1.0f;
                brownNoiseState[ch] = brownAlpha * brownNoiseState[ch]
                                    + (1.0f - brownAlpha) * white;
                buffer.getWritePointer(ch)[s] += (brownNoiseState[ch] / brownNormScale)
                                               * brownLevel;
            }
        }
    }

    DSPUtils::applySoftClip(buffer);
}
