#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "AIProcessor.h"

namespace openchoir
{

class AILookAndFeel : public juce::LookAndFeel_V4
{
public:
    AILookAndFeel();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

class VocalPage;
class ChoirPage;
class MixPage;

class AIEditor : public juce::AudioProcessorEditor,
                 public juce::DragAndDropContainer,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer
{
public:
    explicit AIEditor (AIProcessor&);
    ~AIEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    void showPage (int index);
    int  getPage() const { return page; }
    void loadVocalFile (const juce::File&);
    void browseForVocal();
    AIProcessor& proc;
    float scale() const { return getWidth() / 920.0f; }

private:
    void timerCallback() override;
    bool pageEnabled (int index) const;

    AILookAndFeel lnf;
    std::unique_ptr<VocalPage> vocalPage;
    std::unique_ptr<ChoirPage> choirPage;
    std::unique_ptr<MixPage> mixPage;
    std::array<juce::TextButton, 3> tabs;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };
    int page = 0;
    bool dragHover = false;
    JobRunner::State lastState = JobRunner::State::idle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIEditor)
};

} // namespace openchoir
