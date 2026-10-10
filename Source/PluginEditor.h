#pragma once

#include "PluginProcessor.h"

class ChoirLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChoirLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class OpenChoirAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit OpenChoirAudioProcessorEditor (OpenChoirAudioProcessor&);
    ~OpenChoirAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;   // keeps the preset menu in sync with host/program changes

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void addKnob (const char* paramId, const juce::String& name, const juce::String& tooltip);
    Knob* knob (const char* paramId);

    OpenChoirAudioProcessor& processor;
    ChoirLookAndFeel lnf;
    std::vector<std::pair<juce::String, std::unique_ptr<Knob>>> knobs;
    juce::ComboBox presetBox;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Rectangle<int> ensembleArea, colourArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenChoirAudioProcessorEditor)
};
