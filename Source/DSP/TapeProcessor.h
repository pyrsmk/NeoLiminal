#pragma once
#include <JuceHeader.h>

class TapeProcessor {
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer, float amount);
    void reset();

private:
    double sampleRate = 44100.0;

    // LFOs
    double vibratoPhase = 0.0;
    double wowPhase     = 0.0;
    double flutterPhase = 0.0;

    // Shared delay buffer for vibrato + cassette wow/flutter
    static constexpr size_t kWowBufSize = 8192;
    std::array<std::vector<float>, 2> wowBuffer;
    std::array<size_t, 2> wowWritePos{};

    // 1-pole LP and HP filter states
    float lpState[2] = { 0.0f, 0.0f };
    float hpState[2] = { 0.0f, 0.0f };

    // Band-limited noise for organic wow/flutter modulation
    float wowNoiseLP     = 0.0f;
    float flutterNoiseLP = 0.0f;

    // Brown noise (tape hiss)
    float brownNoiseState[2]  = { 0.0f, 0.0f };
    float brownAlpha           = 0.0f;
    float brownNormScale       = 0.0f;

    // Occasional big flutter burst
    float flutterBurstEnv     = 0.0f;
    float flutterBurstPeak    = 0.0f;
    bool  flutterBurstRising  = false;
    int   flutterBurstCounter = 0; // samples until next burst

    // Smoothers
    juce::SmoothedValue<float> smoothAmount; // 100ms
    juce::SmoothedValue<float> smoothCassT;  // 300ms

    juce::Random random;

    float readInterpolated(const std::vector<float>& buf, size_t writePos, float delaySamples);
};
