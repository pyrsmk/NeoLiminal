#pragma once
#include <JuceHeader.h>

class FeedbackProcessor {
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer, float amount);
    void reset();

private:
    static constexpr size_t kMaxDelaySamples = 384000; // 8s at 48k

    // --- Delay ---
    std::array<std::vector<float>, 2> delayBuffer;
    std::array<size_t, 2> writePos{};
    double sampleRate = 44100.0;

    juce::SmoothedValue<float> smoothDelayMs;
    juce::SmoothedValue<float> smoothFeedback;
    juce::SmoothedValue<float> smoothWetMix;
    juce::SmoothedValue<float> smoothDryMix;

    std::array<juce::dsp::IIR::Filter<float>, 2> lpFilter;

    // --- Descending pitch spiral (50–100%) ---
    float pitchOffset[2]  = { 0.0f, 0.0f }; // primary read head
    float pitchOffsetB[2] = { 0.0f, 0.0f }; // secondary read head (crossfade target)
    float pitchCrossfade  = 0.0f;            // 0=fully A, 1=fully B
    bool  pitchCrossfading = false;
    juce::SmoothedValue<float> smoothPitchRatio;

    // Exit crossfade: pitch→normal transition without click
    bool  pitchExiting   = false;
    float pitchExitFade  = 0.0f;

    // HP filter in feedback loop to prevent DC accumulation
    float hpState[2] = { 0.0f, 0.0f };
    float hpAlpha    = 0.0f;

    float readDelay(size_t channel, float delaySamples);
};
