#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "gui/Layout.h"

class MeteoraNamCompressorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit MeteoraNamCompressorEditor (MeteoraNamCompressorProcessor&);
    ~MeteoraNamCompressorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Invisible controls: the editor paints the artwork, these only handle the mouse.
    struct HiddenSlider : juce::Slider
    {
        HiddenSlider()
        {
            setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            setMouseDragSensitivity (220);
            setVelocityBasedMode (false);
        }
        void paint (juce::Graphics&) override {}
        bool hitTest (int x, int y) override
        {
            const auto c = getLocalBounds().toFloat().getCentre();
            return c.getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;
        }
    };

    struct HiddenToggle : juce::ToggleButton
    {
        void paintButton (juce::Graphics&, bool, bool) override {}
    };

    struct KnobView
    {
        HiddenSlider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        const Layout::Knob* geo = nullptr;
        juce::Image sprite;
        std::function<float (double)> angleOf;
    };

    void timerCallback() override;
    void drawKnob (juce::Graphics&, const KnobView&) const;
    void drawNeedle (juce::Graphics&) const;
    void drawLed (juce::Graphics&, Layout::Pt, bool on) const;
    juce::Rectangle<int> toScreen (juce::Rectangle<float> designRect) const;
    float viewScale() const { return (float) getWidth() / Layout::width; }

    MeteoraNamCompressorProcessor& proc;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Image panel, powerOff, ledOn, glass;

    std::array<KnobView, 6> knobs;
    HiddenToggle vuButton, powerButton, captureButton, ecoButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> vuAtt, powerAtt, captureAtt, ecoAtt;

    float grSmoothed = 0.0f, vuSmoothed = 0.0f, needleAngle = Layout::grAngle (0.0f);
    double clipUntil = 0.0;
    bool clipLit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeteoraNamCompressorEditor)
};
