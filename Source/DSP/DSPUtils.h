#pragma once
#include <JuceHeader.h>

namespace DSPUtils
{

// Transparent soft clip with Hermite cubic knee.
// Unity gain for |x| <= 0.9 (~-0.9 dBFS), C1-smooth knee to ±1, hard clip above.
// Polynomial derived via Hermite conditions: p(0)=0.9, p(1)=1, p'(0)=0.1, p'(1)=0
// → p(t) = -0.1t³ + 0.1t² + 0.1t + 0.9   where t = (|x| - 0.9) / 0.1
static inline void applySoftClip(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
        {
            const float x     = data[s];
            const float abs_x = std::fabs(x);

            if (abs_x <= 0.9f)
                continue;

            if (abs_x >= 1.0f)
            {
                data[s] = std::copysign(1.0f, x);
                continue;
            }

            const float t = (abs_x - 0.9f) * 10.0f; // [0, 1] over knee
            data[s] = std::copysign(-0.1f*t*t*t + 0.1f*t*t + 0.1f*t + 0.9f, x);
        }
    }
}

} // namespace DSPUtils
