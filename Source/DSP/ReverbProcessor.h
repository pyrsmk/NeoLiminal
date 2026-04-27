#pragma once
#include <JuceHeader.h>

class ReverbProcessor {
public:
    void prepare(const juce::dsp::ProcessSpec& spec);
    void process(juce::AudioBuffer<float>& buffer, float amount);
    void reset();

private:
    double sampleRate = 44100.0;

    // Dattorro plate reverb buffers (mono processing then spread to stereo)
    // Input diffusion
    struct AllpassFilter {
        std::vector<float> buffer;
        size_t writePos = 0;
        int size = 0;
        float coeff = 0.0f;
        float process(float input);
        float processModulated(float input, float delaySamples); // interpolated, variable delay
        void prepare(int bufSize, float c);
        void reset();
    };

    struct DelayLine {
        std::vector<float> buffer;
        size_t writePos = 0;
        int size = 0;
        float read(int delaySamples) const;
        void write(float value);
        void prepare(int bufSize);
        void reset();
    };

    // Input diffusion: 4 allpass
    std::array<AllpassFilter, 4> inputDiffusion;

    // Tank A
    AllpassFilter tankA_ap1, tankA_ap2;
    DelayLine tankA_d1, tankA_d2;
    float tankA_lp = 0.0f; // LP state

    // Tank B
    AllpassFilter tankB_ap1, tankB_ap2;
    DelayLine tankB_d1, tankB_d2;
    float tankB_lp = 0.0f;

    float tankA_state = 0.0f;
    float tankB_state = 0.0f;
    float tankEnv     = 0.0f; // AGC envelope for tank energy

    // Granular pitch shifter: two Hann-windowed grains, constant-power crossfade
    struct GranularShimmer {
        std::vector<float> buf;
        size_t writePos      = 0;
        float  readPos       = 0.0f;
        float  readPos2      = 0.0f;
        int    grainSize     = 8192;
        float  speedRatio    = 0.5f;
        void  prepare(float ratio, int grainSz);
        void  reset();
        float process(float input);
    };

    // Stereo shimmer pairs: L fed from tankA, R fed from tankB
    GranularShimmer shimmerDownL, shimmerDownR; // −1 oct
    GranularShimmer shimmerUpL,   shimmerUpR;   // +1 oct

    // Pre-LP for shimmerUp: band-limits input before pitch-shift → smoother grains
    float shimmerUpPreLpL = 0.0f;
    float shimmerUpPreLpR = 0.0f;

    float preDelaySample = 0.0f;
    DelayLine preDelay;

    // Tank LFO modulation (Dattorro: two LFOs at slightly different rates)
    double lfoPhaseA = 0.0;
    double lfoPhaseB = 0.0;


    juce::SmoothedValue<float> smoothAmount;
};