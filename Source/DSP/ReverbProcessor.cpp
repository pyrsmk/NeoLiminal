#include "ReverbProcessor.h"
#include "DSPUtils.h"

// ========================= AllpassFilter =========================

void ReverbProcessor::AllpassFilter::prepare(int bufSize, float c)
{
    size     = bufSize;
    coeff    = c;
    buffer.assign(static_cast<size_t>(bufSize + 1), 0.0f);
    writePos = 0;
}

void ReverbProcessor::AllpassFilter::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float ReverbProcessor::AllpassFilter::process(float input)
{
    const int   bufSize  = static_cast<int>(buffer.size());
    const int   iWritePos = static_cast<int>(writePos);
    const int   readPos  = (iWritePos - size + bufSize) % bufSize;
    const float delayed  = buffer[static_cast<size_t>(readPos)];
    const float v        = input - coeff * delayed;
    const float output   = coeff * v + delayed;
    buffer[writePos]     = v;
    writePos             = (writePos + 1) % static_cast<size_t>(bufSize);
    return output;
}

float ReverbProcessor::AllpassFilter::processModulated(float input, float delaySamples)
{
    const int   bufSize = (int)buffer.size();
    const float fBufSize = (float)bufSize;
    float readF = (float)(int)writePos - delaySamples;
    while (readF <  0.0f)      readF += fBufSize;
    while (readF >= fBufSize)  readF -= fBufSize;
    const size_t i0      = (size_t)(int)readF;
    const size_t i1      = (i0 + 1) % (size_t)bufSize;
    const float  frac    = readF - std::floor(readF);
    const float  delayed = buffer[i0] * (1.0f - frac) + buffer[i1] * frac;
    const float  v       = input - coeff * delayed;
    const float  output  = coeff * v + delayed;
    buffer[writePos] = v;
    writePos = (writePos + 1) % (size_t)bufSize;
    return output;
}

// ========================= DelayLine =========================

void ReverbProcessor::DelayLine::prepare(int bufSize)
{
    size = bufSize;
    buffer.assign(static_cast<size_t>(bufSize + 2), 0.0f);
    writePos = 0;
}

void ReverbProcessor::DelayLine::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void ReverbProcessor::DelayLine::write(float value)
{
    buffer[writePos] = value;
    writePos = (writePos + 1) % buffer.size();
}

// read(N): returns the value written N samples ago (after write() was called)
float ReverbProcessor::DelayLine::read(int delaySamples) const
{
    const int    bufSize   = static_cast<int>(buffer.size());
    const int    iWritePos = static_cast<int>(writePos);
    const size_t idx       = static_cast<size_t>((iWritePos - delaySamples - 1 + bufSize * 2) % bufSize);
    return buffer[idx];
}


// ========================= GranularShimmer =========================

void ReverbProcessor::GranularShimmer::prepare(float ratio, int grainSz)
{
    speedRatio = ratio;
    grainSize  = grainSz;
    buf.assign(65536, 0.0f);
    writePos = 0;
    // Pitch up (ratio >= 1): start read heads one grain behind write so they
    // read valid past data immediately instead of silent future buffer.
    if (speedRatio >= 1.0f)
    {
        readPos  = (float)(65536 - grainSize);
        readPos2 = (float)(65536 - grainSize / 2);
    }
    else
    {
        readPos  = 0.0f;
        readPos2 = (float)(grainSize / 2);
    }
}

void ReverbProcessor::GranularShimmer::reset()
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    writePos = 0;
    if (speedRatio >= 1.0f)
    {
        readPos  = (float)(65536 - grainSize);
        readPos2 = (float)(65536 - grainSize / 2);
    }
    else
    {
        readPos  = 0.0f;
        readPos2 = (float)(grainSize / 2);
    }
}

float ReverbProcessor::GranularShimmer::process(float input)
{
    const int   bufSize  = (int)buf.size();
    const float fBufSize = (float)bufSize;
    const float fGrain   = (float)grainSize;

    buf[writePos] = input;

    auto readInterp = [&](float rp) -> float
    {
        while (rp <  0.0f)      rp += fBufSize;
        while (rp >= fBufSize)  rp -= fBufSize;
        const size_t i0   = (size_t)(int)rp;
        const size_t i1   = (i0 + 1) % (size_t)bufSize;
        const float  frac = rp - std::floor(rp);
        return buf[i0] * (1.0f - frac) + buf[i1] * frac;
    };

    // Phase within current grain: how far the read head lags behind the write head
    auto grainPhase = [&](float rp) -> float
    {
        float dist = (float)(int)writePos - rp;
        if (dist < 0.0f) dist += fBufSize;
        dist = std::fmod(dist, fGrain);
        if (dist < 0.0f) dist += fGrain;
        return dist / fGrain;
    };

    // sqrt-Hann crossfade: w=0 at boundaries (no clicks) + w1²+w2²=1 (constant power, no LFO dip)
    const float ph1 = grainPhase(readPos);
    const float ph2 = grainPhase(readPos2);
    const float w1  = std::sqrt(0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * ph1)));
    const float w2  = std::sqrt(0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * ph2)));
    const float out = readInterp(readPos) * w1 + readInterp(readPos2) * w2;

    writePos = (writePos + 1) % (size_t)bufSize;

    // speedRatio < 1 → read advances slower than write → pitch down
    readPos  += speedRatio;
    readPos2 += speedRatio;
    if (readPos  >= fBufSize) readPos  -= fBufSize;
    if (readPos2 >= fBufSize) readPos2 -= fBufSize;

    return out;
}

// ========================= ReverbProcessor =========================

void ReverbProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const double sc = sampleRate / 29761.0;

    // Input diffusion: 4 series allpass (Dattorro sizes, scaled)
    inputDiffusion[0].prepare((int)(142 * sc) + 1, 0.75f);
    inputDiffusion[1].prepare((int)(107 * sc) + 1, 0.75f);
    inputDiffusion[2].prepare((int)(379 * sc) + 1, 0.625f);
    inputDiffusion[3].prepare((int)(277 * sc) + 1, 0.625f);

    // Tank A (ap2 is LFO-modulated: +32 samples headroom)
    tankA_ap1.prepare((int)(672  * sc) + 1,      0.7f);
    tankA_d1 .prepare((int)(4453 * sc) + 1);
    tankA_ap2.prepare((int)(1800 * sc) + 1 + 32, 0.7f);
    tankA_d2 .prepare((int)(3720 * sc) + 1);

    // Tank B (ap1 is LFO-modulated: +32 samples headroom)
    tankB_ap1.prepare((int)(908  * sc) + 1 + 32, 0.7f);
    tankB_d1 .prepare((int)(4217 * sc) + 1);
    tankB_ap2.prepare((int)(2656 * sc) + 1,      0.7f);
    tankB_d2 .prepare((int)(3163 * sc) + 1);

    preDelay.prepare((int)(0.02 * sampleRate) + 1);

    // Stereo shimmer: L from tankA, R from tankB
    shimmerDownL.prepare(0.5f, 16384); // −1 oct, crossfade ~1.35Hz
    shimmerDownR.prepare(0.5f, 16384);
    shimmerUpL.prepare(2.0f, 32768);  // +1 oct, sqrt-Hann crossfade
    shimmerUpR.prepare(2.0f, 32768);
    shimmerUpPreLpL = 0.0f;
    shimmerUpPreLpR = 0.0f;

    tankA_lp     = 0.0f;
    tankB_lp     = 0.0f;
    tankA_state  = 0.0f;
    tankB_state  = 0.0f;
    tankEnv      = 0.0f;
    preDelaySample = 0.0f;
    lfoPhaseA    = 0.0;
    lfoPhaseB    = juce::MathConstants<double>::halfPi; // 90° offset between LFOs

    smoothAmount.reset(sampleRate, 0.100);
    smoothAmount.setCurrentAndTargetValue(0.0f);
}

void ReverbProcessor::reset()
{
    for (auto& ap : inputDiffusion) ap.reset();
    tankA_ap1.reset();  tankA_ap2.reset();
    tankA_d1.reset();   tankA_d2.reset();
    tankB_ap1.reset();  tankB_ap2.reset();
    tankB_d1.reset();   tankB_d2.reset();
    preDelay.reset();
    shimmerDownL.reset(); shimmerDownR.reset();
    shimmerUpL.reset();   shimmerUpR.reset();
    shimmerUpPreLpL = 0.0f;
    shimmerUpPreLpR = 0.0f;
    tankA_lp     = 0.0f;
    tankB_lp     = 0.0f;
    tankA_state  = 0.0f;
    tankB_state  = 0.0f;
    tankEnv      = 0.0f;
    preDelaySample = 0.0f;
    lfoPhaseA    = 0.0;
    lfoPhaseB    = juce::MathConstants<double>::halfPi;
    smoothAmount.setCurrentAndTargetValue(0.0f);
}

void ReverbProcessor::process(juce::AudioBuffer<float>& buffer, float amount)
{
    smoothAmount.setTargetValue(amount);

    if (amount < 0.001f && !smoothAmount.isSmoothing())
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const double sc = sampleRate / 29761.0;

    // AGC coefficients: 2ms attack (fast response to peaks), 500ms release
    const float agcAttack  = std::exp(-1.0f / (0.002f * (float)sampleRate));
    const float agcRelease = std::exp(-1.0f / (0.500f * (float)sampleRate));

    // Pre-LP for shimmerUp: 2kHz cutoff, band-limits input before pitch-shift
    const float upPreAlpha = std::exp(-juce::MathConstants<float>::twoPi * 2000.0f / (float)sampleRate);

    // Nominal full-delay lengths for each delay line (must match prepare())
    const int dA1      = (int)(4453 * sc) + 1;
    const int dA2      = (int)(3720 * sc) + 1;
    const int dB1      = (int)(4217 * sc) + 1;
    const int dB2      = (int)(3163 * sc) + 1;
    const int preDelLen = (int)(0.02 * sampleRate);

    // Output taps: Dattorro positions scaled to current SR, clamped to valid range
    auto clampTap = [](int v, int maxVal) { return juce::jmax(1, juce::jmin(v, maxVal)); };

    const int tapL_Ad1_266  = clampTap((int)(266  * sc), dA1 - 1);
    const int tapL_Ad1_2974 = clampTap((int)(2974 * sc), dA1 - 1);
    const int tapL_Ad2_1913 = clampTap((int)(1913 * sc), dA2 - 1);
    const int tapL_Bd1_1990 = clampTap((int)(1990 * sc), dB1 - 1);
    const int tapL_Bd2_187  = clampTap((int)(187  * sc), dB2 - 1);
    const int tapL_Bd2_1066 = clampTap((int)(1066 * sc), dB2 - 1);

    const int tapR_Bd1_353  = clampTap((int)(353  * sc), dB1 - 1);
    const int tapR_Bd1_3627 = clampTap((int)(3627 * sc), dB1 - 1);
    const int tapR_Bd2_1228 = clampTap((int)(1228 * sc), dB2 - 1);
    const int tapR_Ad1_2111 = clampTap((int)(2111 * sc), dA1 - 1);
    const int tapR_Ad2_335  = clampTap((int)(335  * sc), dA2 - 1);
    const int tapR_Ad2_121  = clampTap((int)(121  * sc), dA2 - 1);

    // LFO depth: 8 samples at 29761 Hz reference rate, scaled to current SR
    const float lfoDepth    = (float)(8.0 * sampleRate / 29761.0);
    // Nominal (centre) delay for the two modulated allpass filters
    const float nomA_ap2    = (float)((int)(1800 * sc));
    const float nomB_ap1    = (float)((int)(908  * sc));
    static constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

    for (int s = 0; s < numSamples; ++s)
    {
        const float sm = smoothAmount.getNextValue();

        // LFO modulation: two sines at slightly different rates (Dattorro: ~0.1 Hz)
        lfoPhaseA = std::fmod(lfoPhaseA + 0.11 / sampleRate * kTwoPi, kTwoPi);
        lfoPhaseB = std::fmod(lfoPhaseB + 0.13 / sampleRate * kTwoPi, kTwoPi);
        const float modA = (float)std::sin(lfoPhaseA) * lfoDepth;
        const float modB = (float)std::sin(lfoPhaseB) * lfoDepth;

        // Crossfade: steep ramp 0→40% wet in first 1%, then 40→85% over 1–100%
        const float wetMix = sm < 0.01f
            ? sm * 40.0f * 0.40f
            : 0.40f + (sm - 0.01f) * (0.45f / 0.99f);
        const float dryMix = 1.0f - wetMix;

        // Decay: 0.69 at 1% → 0.90 at 100% → T60 ≈ 11s→38s
        const float decay = 0.69f + sm * 0.21f;

        // LP damping in feedback loop: alpha=0.93 (fc≈500Hz) at sm=0,
        // alpha=0.56 (fc≈4kHz, airy) at sm=1. Brighter tail for ambient/angelic character.
        const float lpCoeff = 0.93f - 0.37f * sm;

        // shimmerDown: sqrt curve — reaches kMaxShimmerDown at 50%, holds from 50–100%
        static constexpr float kMaxShimmerDown = 1.00f;
        static constexpr float kMaxShimmerUp   = 0.35f;
        const float shimmerDownMix = sm <= 0.50f
            ? std::sqrt(sm / 0.50f) * kMaxShimmerDown
            : kMaxShimmerDown;

        // shimmerUp: 0 below 50%, sqrt ramp to kMaxShimmerUp over 50–100%
        const float shimmerUpMix = sm <= 0.50f
            ? 0.0f
            : std::sqrt((sm - 0.50f) / 0.50f) * kMaxShimmerUp;

        // Mono mix of all input channels
        float monoIn = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            monoIn += buffer.getReadPointer(ch)[s];
        monoIn /= (float)numChannels;

        // Pre-delay (~20 ms)
        const float preDelayed = preDelay.read(preDelLen);
        preDelay.write(monoIn);

        // Input diffusion: 4 series allpass filters
        float diffused = preDelayed;
        for (auto& ap : inputDiffusion)
            diffused = ap.process(diffused);

        // Snapshot previous states for symmetric cross-coupling (no same-sample feedback)
        const float prevA = tankA_state;
        const float prevB = tankB_state;

        // AGC: track tank energy with fast attack / slow release, reduce gain above unity
        {
            const float tankSignal = juce::jmax(std::fabs(prevA), std::fabs(prevB));
            if (tankSignal > tankEnv)
                tankEnv = agcAttack  * tankEnv + (1.0f - agcAttack)  * tankSignal;
            else
                tankEnv = agcRelease * tankEnv + (1.0f - agcRelease) * tankSignal;
        }
        const float agcGain = tankEnv > 1.0f ? 1.0f / tankEnv : 1.0f;

        // Tank A: ap1 → D1 → ×decay → LP → ap2(LFO-modulated) → D2
        {
            float t = tankA_ap1.process(diffused + prevB);
            tankA_d1.write(t);
            const float d = tankA_d1.read(dA1) * decay * agcGain;
            tankA_lp = d * (1.0f - lpCoeff) + tankA_lp * lpCoeff;
            tankA_d2.write(tankA_ap2.processModulated(tankA_lp, nomA_ap2 + modA));
            tankA_state = tankA_d2.read(dA2);
        }

        // Tank B: ap1(LFO-modulated) → D1 → ×decay → LP → ap2 → D2
        {
            float t = tankB_ap1.processModulated(diffused + prevA, nomB_ap1 + modB);
            tankB_d1.write(t);
            const float d = tankB_d1.read(dB1) * decay * agcGain;
            tankB_lp = d * (1.0f - lpCoeff) + tankB_lp * lpCoeff;
            tankB_d2.write(tankB_ap2.process(tankB_lp));
            tankB_state = tankB_d2.read(dB2);
        }

        // Pre-LP for shimmerUp: smooth the input before pitch-shift to reduce grain artifacts
        shimmerUpPreLpL = (1.0f - upPreAlpha) * tankA_state + upPreAlpha * shimmerUpPreLpL;
        shimmerUpPreLpR = (1.0f - upPreAlpha) * tankB_state + upPreAlpha * shimmerUpPreLpR;

        // Shimmer: stereo granular shifters, L from tankA, R from tankB
        const float shimmerL = shimmerDownL.process(tankA_state)   * shimmerDownMix
                             + shimmerUpL.process(shimmerUpPreLpL) * shimmerUpMix;
        const float shimmerR = shimmerDownR.process(tankB_state)   * shimmerDownMix
                             + shimmerUpR.process(shimmerUpPreLpR) * shimmerUpMix;

        // Stereo output: sums of taps at multiple delay line positions (Dattorro topology).
        // Taps from A-dominant positions go left, B-dominant go right for stereo spread.
        const float outL = ( tankA_d1.read(tapL_Ad1_266)
                           + tankA_d1.read(tapL_Ad1_2974)
                           - tankA_d2.read(tapL_Ad2_1913)
                           + tankB_d1.read(tapL_Bd1_1990)
                           + tankB_d2.read(tapL_Bd2_187)
                           - tankB_d2.read(tapL_Bd2_1066)) * (1.0f / 6.0f);

        const float outR = ( tankB_d1.read(tapR_Bd1_353)
                           + tankB_d1.read(tapR_Bd1_3627)
                           - tankB_d2.read(tapR_Bd2_1228)
                           + tankA_d1.read(tapR_Ad1_2111)
                           - tankA_d2.read(tapR_Ad2_335)
                           + tankA_d2.read(tapR_Ad2_121)) * (1.0f / 6.0f);

        if (numChannels >= 1)
        {
            float* ch0 = buffer.getWritePointer(0);
            ch0[s] = dryMix * ch0[s] + wetMix * (outL + shimmerL);
        }
        if (numChannels >= 2)
        {
            float* ch1 = buffer.getWritePointer(1);
            ch1[s] = dryMix * ch1[s] + wetMix * (outR + shimmerR);
        }
    }

    DSPUtils::applySoftClip(buffer);
}