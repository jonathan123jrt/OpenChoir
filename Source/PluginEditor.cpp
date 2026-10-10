#include "PluginEditor.h"

namespace Colours_
{
    const juce::Colour bg       { 0xff16141c };
    const juce::Colour panel    { 0xff211e29 };
    const juce::Colour edge     { 0xff34303f };
    const juce::Colour gold     { 0xffe2b45a };
    const juce::Colour goldDim  { 0xff7a6232 };
    const juce::Colour text     { 0xffefe8dc };
    const juce::Colour textDim  { 0xff9c95a8 };
}

//==============================================================================
ChoirLookAndFeel::ChoirLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       Colours_::text);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,               Colours_::text);
    setColour (juce::ComboBox::backgroundColourId,      Colours_::panel);
    setColour (juce::ComboBox::outlineColourId,         Colours_::edge);
    setColour (juce::ComboBox::textColourId,            Colours_::text);
    setColour (juce::ComboBox::arrowColourId,           Colours_::gold);
    setColour (juce::PopupMenu::backgroundColourId,     Colours_::panel);
    setColour (juce::PopupMenu::textColourId,           Colours_::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours_::goldDim);
    setColour (juce::TooltipWindow::backgroundColourId, Colours_::panel);
    setColour (juce::TooltipWindow::textColourId,       Colours_::text);
    setColour (juce::TooltipWindow::outlineColourId,    Colours_::edge);
}

void ChoirLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float angle  = startAngle + pos * (endAngle - startAngle);
    const float track  = juce::jmax (3.0f, radius * 0.12f);

    // background arc
    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours_::edge);
    g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    if (pos > 0.001f)
    {
        juce::Path valArc;
        valArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
        g.setColour (s.isEnabled() ? Colours_::gold : Colours_::goldDim);
        g.strokePath (valArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // cap
    const float capR = (radius - track * 2.2f);
    g.setColour (Colours_::panel.brighter (0.08f));
    g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre));

    // pointer
    juce::Path ptr;
    const float pw = juce::jmax (2.0f, capR * 0.12f);
    ptr.addRoundedRectangle (-pw * 0.5f, -capR, pw, capR * 0.55f, pw * 0.5f);
    g.setColour (Colours_::text);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (centre));
}

juce::Label* ChoirLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (12.5f));
    l->setColour (juce::Label::textColourId, Colours_::textDim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

//==============================================================================
OpenChoirAudioProcessorEditor::OpenChoirAudioProcessorEditor (OpenChoirAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    addKnob (ParamIDs::voices,   "Voices",      "How many singers in the main section (1-16).");
    addKnob (ParamIDs::detune,   "Detune",      "How far each singer drifts from perfect pitch, in cents.");
    addKnob (ParamIDs::timing,   "Timing",      "How loosely the singers come in. Raise for gang vocals.");
    addKnob (ParamIDs::vibrato,  "Vibrato",     "Each singer's natural vibrato, at their own speed.");
    addKnob (ParamIDs::width,    "Width",       "Stereo spread of the singers.");
    addKnob (ParamIDs::tone,     "Tone",        "Overall brightness of the choir.");
    addKnob (ParamIDs::octDown,  "Oct Down",    "Adds a section singing an octave below.");
    addKnob (ParamIDs::octUp,    "Oct Up",      "Adds a section singing an octave above.");
    addKnob (ParamIDs::grit,     "Grit",        "Saturation for shouted / gang vocals.");
    addKnob (ParamIDs::room,     "Room",        "How much of the shared room the choir sings in.");
    addKnob (ParamIDs::roomSize, "Room Size",   "From small studio to large hall.");
    addKnob (ParamIDs::choirLvl, "Choir",       "Level of the generated choir.");
    addKnob (ParamIDs::dryLvl,   "Lead",        "Level of your original vocal. Turn to Off for choir only.");

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0)
            processor.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    setResizable (true, true);
    setResizeLimits (640, 380, 1400, 830);
    getConstrainer()->setFixedAspectRatio (780.0 / 460.0);
    setSize (780, 460);
    startTimerHz (4);
}

void OpenChoirAudioProcessorEditor::timerCallback()
{
    const int id = processor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

OpenChoirAudioProcessorEditor::~OpenChoirAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void OpenChoirAudioProcessorEditor::addKnob (const char* paramId, const juce::String& name, const juce::String& tooltip)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    k->slider.setTooltip (tooltip);
    k->slider.setPopupDisplayEnabled (false, false, this);
    addAndMakeVisible (k->slider);

    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    k->label.setColour (juce::Label::textColourId, Colours_::text);
    addAndMakeVisible (k->label);

    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k->slider);
    knobs.emplace_back (paramId, std::move (k));
}

OpenChoirAudioProcessorEditor::Knob* OpenChoirAudioProcessorEditor::knob (const char* paramId)
{
    for (auto& [id, k] : knobs)
        if (id == paramId)
            return k.get();
    return nullptr;
}

void OpenChoirAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float s = getWidth() / 780.0f;
    g.fillAll (Colours_::bg);

    // header
    auto header = getLocalBounds().removeFromTop (juce::roundToInt (64 * s)).toFloat();
    g.setColour (Colours_::gold);
    g.setFont (juce::FontOptions (26.0f * s, juce::Font::bold));
    g.drawText ("OpenChoir", header.reduced (22 * s, 0).removeFromLeft (260 * s), juce::Justification::centredLeft);

    g.setColour (Colours_::textDim);
    g.setFont (juce::FontOptions (12.0f * s));
    g.drawText ("one voice in, a whole section out", header.reduced (22 * s, 0).withTrimmedLeft (150 * s).removeFromLeft (260 * s),
                juce::Justification::centredLeft);

    // panels
    for (auto [area, title] : { std::pair { ensembleArea, "ENSEMBLE" }, std::pair { colourArea, "COLOUR, SPACE & MIX" } })
    {
        g.setColour (Colours_::panel);
        g.fillRoundedRectangle (area.toFloat(), 10.0f * s);
        g.setColour (Colours_::edge);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f * s, 1.0f);

        g.setColour (Colours_::goldDim.brighter (0.4f));
        g.setFont (juce::FontOptions (11.0f * s, juce::Font::bold));
        g.drawText (title, area.reduced (juce::roundToInt (14 * s), juce::roundToInt (8 * s)).removeFromTop (juce::roundToInt (16 * s)),
                    juce::Justification::topLeft);
    }
}

void OpenChoirAudioProcessorEditor::resized()
{
    const float s = getWidth() / 780.0f;
    auto r = getLocalBounds();

    auto header = r.removeFromTop (juce::roundToInt (64 * s));
    presetBox.setBounds (header.reduced (juce::roundToInt (22 * s), juce::roundToInt (16 * s)).removeFromRight (juce::roundToInt (220 * s)));

    r.reduce (juce::roundToInt (16 * s), 0);
    r.removeFromBottom (juce::roundToInt (16 * s));

    const int gap = juce::roundToInt (12 * s);
    const int panelH = (r.getHeight() - gap) / 2;
    ensembleArea = r.removeFromTop (panelH);
    r.removeFromTop (gap);
    colourArea = r;

    auto layoutRow = [&] (juce::Rectangle<int> area, std::initializer_list<const char*> ids, int columns)
    {
        area.reduce (juce::roundToInt (10 * s), 0);
        area.removeFromTop (juce::roundToInt (24 * s));
        area.removeFromBottom (juce::roundToInt (8 * s));
        const int colW = area.getWidth() / columns;
        int i = 0;
        for (auto* id : ids)
        {
            auto cell = area.withX (area.getX() + i * colW).withWidth (colW);
            if (auto* k = knob (id))
            {
                k->label.setFont (juce::FontOptions (13.5f * s, juce::Font::bold));
                k->label.setBounds (cell.removeFromTop (juce::roundToInt (18 * s)));
                k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::roundToInt (80 * s), juce::roundToInt (18 * s));
                k->slider.setBounds (cell.reduced (juce::roundToInt (4 * s), 0));
            }
            ++i;
        }
    };

    layoutRow (ensembleArea, { ParamIDs::voices, ParamIDs::detune, ParamIDs::timing, ParamIDs::vibrato, ParamIDs::width, ParamIDs::tone }, 7);
    layoutRow (colourArea,   { ParamIDs::octDown, ParamIDs::octUp, ParamIDs::grit, ParamIDs::room, ParamIDs::roomSize, ParamIDs::choirLvl, ParamIDs::dryLvl }, 7);

    // centre the six ensemble knobs under the seven-column grid
    const int shift = (ensembleArea.getWidth() - juce::roundToInt (20 * s)) / 7 / 2;
    for (auto* id : { ParamIDs::voices, ParamIDs::detune, ParamIDs::timing, ParamIDs::vibrato, ParamIDs::width, ParamIDs::tone })
        if (auto* k = knob (id))
        {
            k->label.setTopLeftPosition (k->label.getX() + shift, k->label.getY());
            k->slider.setTopLeftPosition (k->slider.getX() + shift, k->slider.getY());
        }
}
