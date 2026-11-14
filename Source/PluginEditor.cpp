#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
juce::Image loadImage (const void* data, int size)
{
    return juce::ImageCache::getFromMemory (data, size);
}

juce::Point<float> polar (Layout::Pt c, float angleDeg, float r)
{
    const float a = juce::degreesToRadians (angleDeg);
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

constexpr float kTimerHz = 60.0f;
}

MeteoraNamCompressorEditor::MeteoraNamCompressorEditor (MeteoraNamCompressorProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    panel    = loadImage (BinaryData::panel_png, BinaryData::panel_pngSize);
    powerOff = loadImage (BinaryData::power_off_png, BinaryData::power_off_pngSize);
    ledOn    = loadImage (BinaryData::led_on_png, BinaryData::led_on_pngSize);
    glass    = loadImage (BinaryData::glass_png, BinaryData::glass_pngSize);

    struct Def { const char* id; const Layout::Knob* geo; const void* img; int imgSize; std::function<float (double)> angle; };
    const Def defs[] = {
        { ParamIDs::threshold, &Layout::threshold, BinaryData::knob_threshold_png, BinaryData::knob_threshold_pngSize,
          [] (double v) { return Layout::thresholdAngle ((float) v); } },
        { ParamIDs::ratio,     &Layout::ratio,     BinaryData::knob_ratio_png, BinaryData::knob_ratio_pngSize,
          [] (double v) { return Layout::ratioAngle ((int) std::round (v)); } },
        { ParamIDs::attack,    &Layout::attack,    BinaryData::knob_attack_png, BinaryData::knob_attack_pngSize,
          [] (double v) { return Layout::attackAngle ((int) std::round (v)); } },
        { ParamIDs::release,   &Layout::release,   BinaryData::knob_release_png, BinaryData::knob_release_pngSize,
          [] (double v) { return Layout::releaseAngle ((int) std::round (v)); } },
        { ParamIDs::makeup,    &Layout::makeup,    BinaryData::knob_makeup_png, BinaryData::knob_makeup_pngSize,
          [] (double v) { return Layout::makeupAngle ((float) v); } },
        { ParamIDs::mix,       &Layout::mix,       BinaryData::knob_mix_png, BinaryData::knob_mix_pngSize,
          [] (double v) { return Layout::mixAngle ((float) v); } },
    };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.geo = defs[i].geo;
        k.sprite = loadImage (defs[i].img, defs[i].imgSize);
        k.angleOf = defs[i].angle;
        k.slider.setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, defs[i].id, k.slider);
        if (auto* param = proc.apvts.getParameter (defs[i].id))
            k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        k.slider.onValueChange = [this] { repaint(); };
    }

    for (auto* b : { &vuButton, &powerButton, &captureButton, &ecoButton })
    {
        addAndMakeVisible (*b);
        b->onStateChange = [this] { repaint(); };
        b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    vuAtt      = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::vuMode, vuButton);
    powerAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::power, powerButton);
    captureAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::capture, captureButton);
    ecoAtt     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::eco, ecoButton);

    vuButton.setTooltip ("Meter: Gain Reduction / Output VU");
    captureButton.setTooltip ("Capture: original NAM captures of the compressor (Release AUTO = fast capture, other values = slow capture)");
    ecoButton.setTooltip ("Eco: lighter neural network, lower CPU");

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) Layout::width / Layout::height);
    setResizeLimits ((int) (Layout::width * 0.4f), (int) (Layout::height * 0.4f),
                     (int) Layout::width, (int) Layout::height);
    setSize ((int) std::round (Layout::width * proc.editorScale), (int) std::round (Layout::height * proc.editorScale));

    startTimerHz ((int) kTimerHz);
}

MeteoraNamCompressorEditor::~MeteoraNamCompressorEditor()
{
    stopTimer();
}

juce::Rectangle<int> MeteoraNamCompressorEditor::toScreen (juce::Rectangle<float> r) const
{
    const float s = viewScale();
    return (r * s).getSmallestIntegerContainer();
}

void MeteoraNamCompressorEditor::resized()
{
    proc.editorScale = viewScale();
    auto circle = [] (Layout::Pt c, float r) { return juce::Rectangle<float> (c.x - r, c.y - r, 2 * r, 2 * r); };

    for (auto& k : knobs)
        k.slider.setBounds (toScreen (circle (k.geo->c, k.geo->hitR)));

    vuButton.setBounds (toScreen (circle (Layout::vuButton, Layout::vuButtonR)));
    powerButton.setBounds (toScreen (circle (Layout::power, Layout::powerHitR)));
    captureButton.setBounds (toScreen ({ Layout::captureLed.x - 11.0f, Layout::captureLed.y - 12.0f, Layout::captureHitW, 24.0f }));
    ecoButton.setBounds (toScreen ({ Layout::ecoLed.x - 11.0f, Layout::ecoLed.y - 12.0f, Layout::ecoHitW, 24.0f }));
}

void MeteoraNamCompressorEditor::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.addTransform (juce::AffineTransform::scale (viewScale()));

    g.drawImage (panel, { 0.0f, 0.0f, Layout::width, Layout::height });

    for (const auto& k : knobs)
        drawKnob (g, k);

    drawNeedle (g);

    const bool powerOn = powerButton.getToggleState();
    if (! powerOn)
        g.drawImage (powerOff, juce::Rectangle<int> (Layout::powerSpriteX, Layout::powerSpriteY,
                                                     Layout::powerSpriteSize, Layout::powerSpriteSize).toFloat());

    if (clipLit && powerOn)
        g.drawImage (ledOn, juce::Rectangle<int> (Layout::ledSpriteX, Layout::ledSpriteY,
                                                  Layout::ledSpriteSize, Layout::ledSpriteSize).toFloat());

    // VU button: pressed look + small red indicator when the meter shows output level
    if (vuButton.getToggleState())
    {
        const auto c = Layout::vuButton;
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillEllipse (c.x - 24.0f, c.y - 24.0f, 48.0f, 48.0f);
        drawLed (g, c, true);
    }

    drawLed (g, Layout::captureLed, captureButton.getToggleState());
    drawLed (g, Layout::ecoLed, ecoButton.getToggleState());

    if (! proc.modelsLoaded())
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText ("Model loading error", juce::Rectangle<float> (Layout::faceX, Layout::faceY + 4, Layout::faceW, 18),
                    juce::Justification::centred);
    }
}

void MeteoraNamCompressorEditor::drawKnob (juce::Graphics& g, const KnobView& k) const
{
    const auto& geo = *k.geo;
    g.drawImage (k.sprite, juce::Rectangle<int> (geo.spriteX, geo.spriteY, geo.spriteSize, geo.spriteSize).toFloat());

    const float angle = k.angleOf (k.slider.getValue());
    const auto a = polar (geo.c, angle, 3.0f);
    const auto b = polar (geo.c, angle, geo.pointerLen);
    const float w = geo.pointerWidth;

    juce::Path line;
    line.startNewSubPath (a);
    line.lineTo (b);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (line, juce::PathStrokeType (w + 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (0.8f, 1.3f));
    g.setColour (juce::Colour (234, 229, 214));
    g.strokePath (line, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MeteoraNamCompressorEditor::drawNeedle (juce::Graphics& g) const
{
    const juce::Rectangle<float> face (Layout::faceX, Layout::faceY, Layout::faceW, Layout::faceH);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (face.toNearestInt());

    juce::Path needle;
    needle.startNewSubPath (polar (Layout::pivot, needleAngle, Layout::needleInner));
    needle.lineTo (polar (Layout::pivot, needleAngle, Layout::needleOuter));

    g.setColour (juce::Colour (120, 70, 20).withAlpha (0.16f));     // soft shadow on the face
    g.strokePath (needle, juce::PathStrokeType (4.0f), juce::AffineTransform::translation (4.0f, 2.0f));
    g.setColour (juce::Colour (20, 16, 13));
    g.strokePath (needle, juce::PathStrokeType (Layout::needleWidth));

    if (! powerButton.getToggleState())
    {
        g.setColour (juce::Colours::black.withAlpha (0.40f));   // meter lamp off
        g.fillRect (face);
    }

    g.drawImage (glass, face);                                 // glass: reflections + edge shadow, above the needle
}

void MeteoraNamCompressorEditor::drawLed (juce::Graphics& g, Layout::Pt c, bool on) const
{
    if (! on) return;
    const juce::Colour core (255, 96, 60);
    const float r = Layout::switchLedR;
    juce::ColourGradient halo (core.withAlpha (0.55f), c.x, c.y, core.withAlpha (0.0f), c.x + r * 2.6f, c.y, true);
    g.setGradientFill (halo);
    g.fillEllipse (c.x - r * 2.6f, c.y - r * 2.6f, r * 5.2f, r * 5.2f);
    juce::ColourGradient body (juce::Colour (255, 222, 192), c.x - r * 0.25f, c.y - r * 0.35f, core, c.x + r, c.y + r, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
}

void MeteoraNamCompressorEditor::timerCallback()
{
    auto& m = proc.meters;
    const float gr = m.grPeakDb.exchange (0.0f);
    const float sumSq = m.outSumSq.exchange (0.0f);
    const int count = m.outCount.exchange (0);
    const bool clipped = m.clipped.exchange (false);

    const float dt = 1.0f / kTimerHz;
    auto coef = [dt] (float tau) { return 1.0f - std::exp (-dt / tau); };

    // gain reduction: fast attack, VU-like return
    grSmoothed += (gr - grSmoothed) * (gr > grSmoothed ? coef (0.010f) : coef (0.22f));
    // output VU: ~300 ms integration
    if (count > 0)
    {
        const float rms = std::sqrt (sumSq / (float) count);
        vuSmoothed += (rms - vuSmoothed) * coef (0.085f);
    }

    const bool powerOn = powerButton.getToggleState();
    float target;
    if (vuButton.getToggleState())
    {
        const float ref = 0.12589f;                               // 0 VU = -18 dBFS RMS
        const float db = 20.0f * std::log10 (std::max (vuSmoothed, 1.0e-6f) / ref);
        target = powerOn ? Layout::vuAngle (db) : Layout::angMin;
    }
    else
    {
        target = Layout::grAngle (powerOn ? grSmoothed : 0.0f);
    }
    const float newAngle = needleAngle + (target - needleAngle) * coef (0.035f);   // needle inertia

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (clipped) clipUntil = now + 0.4;
    const bool newClip = now < clipUntil;

    if (std::abs (newAngle - needleAngle) > 0.01f)
    {
        needleAngle = newAngle;
        repaint (toScreen ({ Layout::faceX, Layout::faceY, Layout::faceW, Layout::faceH }).expanded (2));
    }
    if (newClip != clipLit)
    {
        clipLit = newClip;
        repaint (toScreen (juce::Rectangle<int> (Layout::ledSpriteX, Layout::ledSpriteY,
                                                 Layout::ledSpriteSize, Layout::ledSpriteSize).toFloat()).expanded (2));
    }
}
