#include "PluginEditor.h"
#include "AppVersion.h"

//==============================================================================
// LiminalLookAndFeel
//==============================================================================

void LiminalLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPosProportional,
                                          float rotaryStartAngle,
                                          float rotaryEndAngle,
                                          juce::Slider& /*slider*/)
{
    const float cx     = static_cast<float>(x) + width  * 0.5f;
    const float cy     = static_cast<float>(y) + height * 0.5f;
    const float radius = juce::jmin(width, height) * 0.5f - 4.0f;
    const float angle  = rotaryStartAngle
                       + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Outer glow rings (concentric, fading outward)
    for (int i = 5; i >= 1; --i)
    {
        const float gr    = radius + static_cast<float>(i) * 4.5f;
        const float alpha = 0.028f * static_cast<float>(6 - i);
        g.setColour(knobColour.withAlpha(alpha));
        g.fillEllipse(cx - gr, cy - gr, gr * 2.0f, gr * 2.0f);
    }

    // Track arc — full range, dark
    {
        const float tr = radius - 3.0f;
        juce::Path track;
        track.addCentredArc(cx, cy, tr, tr, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff2A1048));
        g.strokePath(track, juce::PathStrokeType(4.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Value arc — glow pass then crisp pass
    if (sliderPosProportional > 0.001f)
    {
        const float tr = radius - 3.0f;
        juce::Path val;
        val.addCentredArc(cx, cy, tr, tr, 0.0f,
                          rotaryStartAngle, angle, true);

        g.setColour(knobColour.withAlpha(0.22f));
        g.strokePath(val, juce::PathStrokeType(9.0f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(knobColour.withAlpha(0.95f));
        g.strokePath(val, juce::PathStrokeType(3.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Center knob body
    const float kr = radius * 0.60f;

    g.setColour(juce::Colours::black.withAlpha(0.50f));
    g.fillEllipse(cx - kr + 1.5f, cy - kr + 1.5f, kr * 2.0f, kr * 2.0f);

    {
        juce::ColourGradient grad(
            juce::Colour(0xff1E0A3A), cx - kr * 0.4f, cy - kr * 0.4f,
            juce::Colour(0xff0D051A), cx + kr * 0.4f, cy + kr * 0.4f,
            false);
        g.setGradientFill(grad);
        g.fillEllipse(cx - kr, cy - kr, kr * 2.0f, kr * 2.0f);
    }

    g.setColour(knobColour.withAlpha(0.13f));
    g.drawEllipse(cx - kr, cy - kr, kr * 2.0f, kr * 2.0f, 1.0f);

    // Pointer line (angles in clockwise-from-top convention)
    {
        const float r0 = kr * 0.18f;
        const float r1 = kr * 0.84f;

        const float px0 = cx + std::sin(angle) * r0;
        const float py0 = cy - std::cos(angle) * r0;
        const float px1 = cx + std::sin(angle) * r1;
        const float py1 = cy - std::cos(angle) * r1;

        g.setColour(knobColour.withAlpha(0.55f));
        g.drawLine(px0, py0, px1, py1, 3.0f);

        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawLine(px0, py0, px1, py1, 1.5f);
    }
}

//==============================================================================
// Shared slider setup
//==============================================================================

void NeoLiminalAudioProcessorEditor::setupSlider(juce::Slider& slider,
                                               LiminalLookAndFeel& laf,
                                               juce::Colour colour)
{
    laf.setKnobColour(colour);
    slider.setLookAndFeel(&laf);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange(0.0, 100.0);
    // -150° to +150° in clockwise-from-top radians
    slider.setRotaryParameters(
        -juce::MathConstants<float>::pi * 5.0f / 6.0f,
         juce::MathConstants<float>::pi * 5.0f / 6.0f,
        true);
    addAndMakeVisible(slider);
}

//==============================================================================
// Constructor / Destructor
//==============================================================================

NeoLiminalAudioProcessorEditor::NeoLiminalAudioProcessorEditor(NeoLiminalAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      attachFeedback   (p.apvts, "feedback",    sliderFeedback),
      attachReverb     (p.apvts, "reverb",      sliderReverb),
      attachDataloss   (p.apvts, "dataloss",    sliderDataloss),
      attachTape       (p.apvts, "tape",        sliderTape)
{
    setupSlider(sliderFeedback, lookAndFeelFeedback, juce::Colour(0xff15F5BA));
    setupSlider(sliderReverb,   lookAndFeelReverb,   juce::Colour(0xff8C00FF));
    setupSlider(sliderDataloss, lookAndFeelDataloss, juce::Colour(0xffFF3F7F));
    setupSlider(sliderTape,     lookAndFeelTape,     juce::Colour(0xffFFC400));

    freezeButton.onClick = [this] {
        audioProcessor.freezeRequest.store(freezeButton.getToggleState());
    };
    addAndMakeVisible(freezeButton);
    addAndMakeVisible(scanlineOverlay);  // last → on top of all children

    setSize(520, 520);
}

NeoLiminalAudioProcessorEditor::~NeoLiminalAudioProcessorEditor()
{
    sliderFeedback.setLookAndFeel(nullptr);
    sliderReverb.setLookAndFeel(nullptr);
    sliderDataloss.setLookAndFeel(nullptr);
    sliderTape.setLookAndFeel(nullptr);
}

//==============================================================================
// Layout
//==============================================================================

void NeoLiminalAudioProcessorEditor::resized()
{
    const int ks   = 110;
    const int half = ks / 2;

    sliderFeedback.setBounds   (140 - half, 175 - half, ks, ks);
    sliderReverb.setBounds     (380 - half, 175 - half, ks, ks);
    sliderDataloss.setBounds   (380 - half, 375 - half, ks, ks);
    sliderTape.setBounds       (140 - half, 375 - half, ks, ks);

    const int btnSize = 48;
    freezeButton.setBounds(260 - btnSize / 2, 275 - btnSize / 2, btnSize, btnSize);

    scanlineOverlay.setBounds(getLocalBounds());
}

//==============================================================================
// Symbol: Feedback — ouroboros spiral with arrow tip
//==============================================================================

void NeoLiminalAudioProcessorEditor::drawSymbolFeedback(juce::Graphics& g,
                                                      float cx, float cy)
{
    juce::Path spiral;
    const float rMin  = 4.5f;
    const float rMax  = 12.0f;
    const int   steps = 80;

    for (int i = 0; i <= steps; ++i)
    {
        const float t   = static_cast<float>(i) / static_cast<float>(steps);
        const float ang = t * juce::MathConstants<float>::twoPi * 1.6f
                        - juce::MathConstants<float>::halfPi;
        const float r   = rMin + (rMax - rMin) * t;

        const float px = cx + std::cos(ang) * r;
        const float py = cy + std::sin(ang) * r;

        if (i == 0) spiral.startNewSubPath(px, py);
        else        spiral.lineTo(px, py);
    }

    g.strokePath(spiral, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Arrow head at the spiral's end point
    const float endAng = juce::MathConstants<float>::twoPi * 1.6f
                       - juce::MathConstants<float>::halfPi;
    const float ex   = cx + std::cos(endAng) * rMax;
    const float ey   = cy + std::sin(endAng) * rMax;
    // Tangent is approximately perpendicular to radius (angular velocity dominates)
    const float fwdX = -std::sin(endAng);
    const float fwdY =  std::cos(endAng);
    const float sdX  =  std::cos(endAng);
    const float sdY  =  std::sin(endAng);
    const float aw   = 3.5f;

    juce::Path arrowHead;
    arrowHead.startNewSubPath(ex - fwdX * aw + sdX * aw * 0.5f,
                               ey - fwdY * aw + sdY * aw * 0.5f);
    arrowHead.lineTo(ex, ey);
    arrowHead.lineTo(ex - fwdX * aw - sdX * aw * 0.5f,
                     ey - fwdY * aw - sdY * aw * 0.5f);
    g.strokePath(arrowHead, juce::PathStrokeType(1.3f,
        juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

//==============================================================================
// Symbol: Reverb — three concentric WiFi-style arcs
//==============================================================================

void NeoLiminalAudioProcessorEditor::drawSymbolReverb(juce::Graphics& g,
                                                    float cx, float cy)
{
    // Dot at bottom; arcs expand upward above it
    // addArc convention: 0=right, CW on screen for increasing angle.
    // Upper semicircle: from π (left/9 o'clock) going CW through top to 2π (right/3 o'clock).
    const float dotY = cy + 9.0f;

    for (int i = 1; i <= 3; ++i)
    {
        const float r = 3.0f + static_cast<float>(i) * 4.0f;
        juce::Path arc;
        arc.addArc(cx - r, dotY - r, r * 2.0f, r * 2.0f,
                   juce::MathConstants<float>::pi * 0.75f,
                   juce::MathConstants<float>::pi * 2.25f,
                   true);
        g.strokePath(arc, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }

    g.fillEllipse(cx - 1.5f, dotY - 1.5f, 3.0f, 3.0f);
}

//==============================================================================
// Symbol: Dataloss — active waveform that drops to flatline (signal dropout)
//==============================================================================

void NeoLiminalAudioProcessorEditor::drawSymbolDataloss(juce::Graphics& g,
                                                      float cx, float cy)
{
    // Random-looking noise waveform
    const float hw  = 12.0f;
    const float amp = 5.5f;

    const float samples[] = {
        0.3f, 1.0f, -0.6f, 0.8f, -1.0f, 0.5f, -0.7f, 0.2f
    };
    constexpr int N = (int)(sizeof(samples) / sizeof(samples[0]));

    juce::Path p;
    for (int i = 0; i < N; ++i)
    {
        const float t  = static_cast<float>(i) / (N - 1);
        const float wx = cx - hw + t * hw * 2.0f;
        const float wy = cy - amp * samples[i];
        if (i == 0) p.startNewSubPath(wx, wy);
        else        p.lineTo(wx, wy);
    }

    g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));
}

//==============================================================================
// Symbol: Tape — two reels connected by a wavy line
//==============================================================================

void NeoLiminalAudioProcessorEditor::drawSymbolTape(juce::Graphics& g,
                                                  float cx, float cy)
{
    const float reel = 5.0f;
    const float lx   = cx - 11.0f;
    const float rx   = cx + 11.0f;

    // Left reel
    g.drawEllipse(lx - reel, cy - reel, reel * 2.0f, reel * 2.0f, 1.3f);
    g.fillEllipse(lx - 1.5f, cy - 1.5f, 3.0f, 3.0f);

    // Right reel
    g.drawEllipse(rx - reel, cy - reel, reel * 2.0f, reel * 2.0f, 1.3f);
    g.fillEllipse(rx - 1.5f, cy - 1.5f, 3.0f, 3.0f);

    // Wavy tape between the two reels
    juce::Path tape;
    const float x0 = lx + reel;
    const float x1 = rx - reel;
    const int   N  = 32;

    for (int i = 0; i <= N; ++i)
    {
        const float t  = static_cast<float>(i) / static_cast<float>(N);
        const float wx = x0 + t * (x1 - x0);
        const float wy = cy + std::sin(t * juce::MathConstants<float>::twoPi * 1.5f) * 3.5f;

        if (i == 0) tape.startNewSubPath(wx, wy);
        else        tape.lineTo(wx, wy);
    }

    g.strokePath(tape, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

//==============================================================================
// paint()
//==============================================================================

void NeoLiminalAudioProcessorEditor::paint(juce::Graphics& g)
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight());

    // Base background
    g.fillAll(juce::Colour(0xff3E1E68));

    // Title — synthwave style, left-aligned with left knobs (x=75)
    {
        const juce::String titleText = "NEO LIMINAL";
        const int   titleX = 85;
        const int   titleY = 38;
        const int   titleW = (int)w;
        const int   titleH = 48;

        const juce::Colour rose(0xffFF2DD1);

        g.setFont(juce::Font(juce::FontOptions(38.0f).withStyle("Bold Italic")));

        // Rose border — 1px offset in all directions
        g.setColour(rose);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    g.drawText(titleText, titleX + dx, titleY + dy,
                               titleW, titleH, juce::Justification::centredLeft, false);

        // Crisp rose main text
        g.setColour(rose);
        g.drawText(titleText, titleX, titleY, titleW, titleH,
                   juce::Justification::centredLeft, false);

        // Version string — right of title, bottom-aligned, pictogram colour
        const juce::String versionStr = juce::String(getVersionString());

        juce::GlyphArrangement ga;
        ga.addLineOfText(g.getCurrentFont(), titleText, 0.0f, 0.0f);
        const float titleTextWidth = ga.getBoundingBox(0, -1, true).getWidth();
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.setColour(juce::Colour(0xffFFACAC));
        g.drawText(versionStr,
                   (int)(titleX + titleTextWidth + 10),
                   titleY + titleH - 25,
                   (int)(w - titleX - titleTextWidth - 10),
                   18,
                   juce::Justification::centredLeft,
                   false);

    }

    // Radial vignette
    {
        juce::ColourGradient vignette(
            juce::Colour(0x00000000), w * 0.5f, h * 0.5f,
            juce::Colour(0xBB000000), 0.0f,     0.0f,
            true);
        g.setGradientFill(vignette);
        g.fillRect(0.0f, 0.0f, w, h);
    }

    // Knob centers
    constexpr float fX = 140.0f, fY = 175.0f;
    constexpr float rX = 380.0f, rY = 175.0f;
    constexpr float cX = 380.0f, cY = 375.0f;
    constexpr float tX = 140.0f, tY = 375.0f;

    // Square connecting lines
    g.setColour(juce::Colour(0xff2A1048));
    g.drawLine(fX, fY, rX, rY, 1.0f);
    g.drawLine(rX, rY, cX, cY, 1.0f);
    g.drawLine(cX, cY, tX, tY, 1.0f);
    g.drawLine(tX, tY, fX, fY, 1.0f);

    // Diagonal cross (dimmer)
    g.setColour(juce::Colour(0xff1A0838));
    g.drawLine(fX, fY, cX, cY, 0.5f);
    g.drawLine(rX, rY, tX, tY, 0.5f);

    // Cryptic symbols below each knob (20px below knob edge)
    g.setColour(juce::Colour(0xffFFACAC));
    constexpr float kSymOffset = 55.0f + 22.0f; // half knob height + gap

    drawSymbolFeedback   (g, fX, fY + kSymOffset);
    drawSymbolReverb     (g, rX, rY + kSymOffset);
    drawSymbolDataloss   (g, cX, cY + kSymOffset);
    drawSymbolTape       (g, tX, tY + kSymOffset);
}
