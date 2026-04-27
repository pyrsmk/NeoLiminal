#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class FreezeButton : public juce::Button
{
public:
    FreezeButton() : juce::Button("Freeze") { setClickingTogglesState(true); }

    void paintButton(juce::Graphics& g, bool /*hovered*/, bool /*held*/) override
    {
        const float cx     = getWidth()  * 0.5f;
        const float cy     = getHeight() * 0.5f;
        const float r      = juce::jmin(getWidth(), getHeight()) * 0.5f - 2.0f;
        const bool  active = getToggleState();
        const juce::Colour rose(0xffFF2DD1);

        if (active)
        {
            g.setColour(rose);
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff1A0838));
            g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
            g.setColour(juce::Colour(0xff2A1048));
            g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);
        }

        // Snowflake — same colour whether active or not
        const juce::Colour iconCol(0xff3D2060);
        g.setColour(iconCol);
        const float armLen    = r * 0.55f;
        const float branchLen = armLen * 0.38f;
        const float branchAng = juce::MathConstants<float>::pi / 4.0f;

        for (int i = 0; i < 6; ++i)
        {
            const float ang = (float)i * juce::MathConstants<float>::pi / 3.0f;
            const float ex  = cx + std::cos(ang) * armLen;
            const float ey  = cy + std::sin(ang) * armLen;
            g.drawLine(cx, cy, ex, ey, 1.4f);

            const float bx = cx + std::cos(ang) * armLen * 0.58f;
            const float by = cy + std::sin(ang) * armLen * 0.58f;
            g.drawLine(bx, by,
                       bx + std::cos(ang + branchAng) * branchLen,
                       by + std::sin(ang + branchAng) * branchLen, 1.4f);
            g.drawLine(bx, by,
                       bx + std::cos(ang - branchAng) * branchLen,
                       by + std::sin(ang - branchAng) * branchLen, 1.4f);
        }
        g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }
};

class ScanlineOverlay : public juce::Component
{
public:
    ScanlineOverlay()
    {
        setInterceptsMouseClicks(false, false);
        setPaintingIsUnclipped(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colours::black.withAlpha(0.16f));
        for (int sy = 0; sy < getHeight(); sy += 2)
            g.drawHorizontalLine(sy, 0.0f, (float)getWidth());
    }
};

class LiminalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LiminalLookAndFeel()  = default;
    ~LiminalLookAndFeel() override = default;

    void setKnobColour(juce::Colour colour) { knobColour = colour; }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

private:
    juce::Colour knobColour { 0xff00F5FF };
};

class NeoLiminalAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit NeoLiminalAudioProcessorEditor(NeoLiminalAudioProcessor&);
    ~NeoLiminalAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    NeoLiminalAudioProcessor& audioProcessor;

    LiminalLookAndFeel lookAndFeelFeedback;
    LiminalLookAndFeel lookAndFeelReverb;
    LiminalLookAndFeel lookAndFeelDataloss;
    LiminalLookAndFeel lookAndFeelTape;

    juce::Slider sliderFeedback;
    juce::Slider sliderReverb;
    juce::Slider sliderDataloss;
    juce::Slider sliderTape;

    juce::AudioProcessorValueTreeState::SliderAttachment attachFeedback;
    juce::AudioProcessorValueTreeState::SliderAttachment attachReverb;
    juce::AudioProcessorValueTreeState::SliderAttachment attachDataloss;
    juce::AudioProcessorValueTreeState::SliderAttachment attachTape;

    void drawSymbolFeedback(juce::Graphics& g, float cx, float cy);
    void drawSymbolReverb(juce::Graphics& g, float cx, float cy);
    void drawSymbolDataloss(juce::Graphics& g, float cx, float cy);
    void drawSymbolTape(juce::Graphics& g, float cx, float cy);

    void setupSlider(juce::Slider& slider, LiminalLookAndFeel& laf, juce::Colour colour);

    FreezeButton freezeButton;
    ScanlineOverlay scanlineOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeoLiminalAudioProcessorEditor)
};