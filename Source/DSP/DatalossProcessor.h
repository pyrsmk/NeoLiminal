#pragma once
#include <JuceHeader.h>

class DatalossProcessor {
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer, float amount);
    void reset();

private:
    double sampleRate = 44100.0;

    // Compressor state machine: COOLDOWN → ATTACK → HOLD → RELEASE → COOLDOWN
    float envelope       = 0.0f;
    float gainSmoothed   = 1.0f;
    float compDropTarget = 1.0f; // target gain at trigger (stored for hold phase)
    int   cooldownCounter = 0;
    int   holdCounter    = 0;
    bool  compActive     = false;
    bool  compHolding    = false;
    bool  compReleasing  = false;
    juce::Random random;

    // Precomputed per prepare()
    float attackCoeff      = 0.0f; // envelope follower: fast attack
    float releaseCoeff     = 0.0f; // envelope follower: moderate release
    float gainAttackCoeff  = 0.0f; // gain drop: ~5ms
    float gainReleaseCoeff = 0.0f; // gain recovery: ~80ms

    // Tube saturation DC blocker state (per channel)
    float dcBlockX[2]  = { 0.0f, 0.0f };
    float dcBlockY[2]  = { 0.0f, 0.0f };
    float satCompGain  = 1.0f; // smoothed RMS compensation gain (avoids block-boundary clicks)

    // Input-adaptive pre-normalisation (keeps saturation character level-consistent)
    float inputRmsFollower     = 0.0f;
    float inputRmsFollowerCoeff = 0.0f; // ~200ms
    float satPreGain           = 1.0f; // smoothed pre-gain (only reduces, never boosts)

    juce::SmoothedValue<float> smoothAmount;
};
