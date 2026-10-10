#include "AIEditor.h"

namespace openchoir
{

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------
namespace C
{
    const juce::Colour bg      { 0xff14121a };
    const juce::Colour panel   { 0xff1f1c27 };
    const juce::Colour panel2  { 0xff2a2634 };
    const juce::Colour edge    { 0xff383246 };
    const juce::Colour gold    { 0xffe2b45a };
    const juce::Colour goldDim { 0xff8a6d36 };
    const juce::Colour text    { 0xffefe8dc };
    const juce::Colour dim     { 0xff9c95a8 };
    const juce::Colour good    { 0xff7fc28a };
    const juce::Colour warn    { 0xffe59a5b };
}

static juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// ---------------------------------------------------------------------------
// Look and feel
// ---------------------------------------------------------------------------
AILookAndFeel::AILookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, C::bg);
    setColour (juce::Label::textColourId, C::text);
    setColour (juce::TextButton::buttonColourId, C::panel2);
    setColour (juce::TextButton::buttonOnColourId, C::gold);
    setColour (juce::TextButton::textColourOffId, C::text);
    setColour (juce::TextButton::textColourOnId, C::bg);
    setColour (juce::ComboBox::backgroundColourId, C::panel2);
    setColour (juce::ComboBox::outlineColourId, C::edge);
    setColour (juce::ComboBox::textColourId, C::text);
    setColour (juce::ComboBox::arrowColourId, C::gold);
    setColour (juce::PopupMenu::backgroundColourId, C::panel);
    setColour (juce::PopupMenu::textColourId, C::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, C::goldDim);
    setColour (juce::Slider::textBoxTextColourId, C::dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, C::panel);
    setColour (juce::TooltipWindow::textColourId, C::text);
    setColour (juce::TooltipWindow::outlineColourId, C::edge);
}

void AILookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (5.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float t = juce::jmax (3.0f, r * 0.13f);
    const float ang = a0 + pos * (a1 - a0);
    juce::Path bgArc, valArc;
    bgArc.addCentredArc (c.x, c.y, r - t, r - t, 0, a0, a1, true);
    valArc.addCentredArc (c.x, c.y, r - t, r - t, 0, a0, ang, true);
    g.setColour (C::edge);
    g.strokePath (bgArc, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (C::gold);
    if (pos > 0.002f) g.strokePath (valArc, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float cr = r - t * 2.3f;
    g.setColour (C::panel2);
    g.fillEllipse (juce::Rectangle<float> (cr * 2, cr * 2).withCentre (c));
    juce::Path p;
    p.addRoundedRectangle (-1.5f, -cr, 3.0f, cr * 0.55f, 1.5f);
    g.setColour (C::text);
    g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c));
}

void AILookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + h * 0.5f;
    const float th = 4.0f;
    g.setColour (C::edge);
    g.fillRoundedRectangle ((float) x, cy - th / 2, (float) w, th, 2.0f);
    g.setColour (s.isEnabled() ? C::gold : C::goldDim);
    g.fillRoundedRectangle ((float) x, cy - th / 2, pos - (float) x, th, 2.0f);
    g.setColour (C::text);
    g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void AILookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const bool primary = b.getProperties()["primary"];
    juce::Colour fill = primary ? C::gold : (on ? C::gold.withAlpha (0.18f) : C::panel2);
    if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.4f);
    else if (down) fill = fill.darker (0.15f);
    else if (over) fill = fill.brighter (0.08f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on || primary ? C::gold : C::edge);
    g.drawRoundedRectangle (r, 6.0f, on ? 1.5f : 1.0f);
}

void AILookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getProperties()["primary"];
    g.setFont (getTextButtonFont (b, b.getHeight()));
    auto col = primary ? C::bg : (b.getToggleState() ? C::gold : C::text);
    g.setColour (b.isEnabled() ? col : col.withAlpha (0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 2);
}

void AILookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (C::panel2);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (C::edge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
    g.setColour (box.isEnabled() ? C::gold : C::goldDim);
    g.strokePath (arrow, juce::PathStrokeType (1.6f));
}

juce::Font AILookAndFeel::getComboBoxFont (juce::ComboBox& b) { return font (juce::jmin (15.0f, b.getHeight() * 0.48f)); }
juce::Font AILookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    const bool primary = b.getProperties()["primary"];
    return font (juce::jmin (primary ? 18.0f : 14.5f, h * (primary ? 0.4f : 0.52f)), primary);
}

juce::Label* AILookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (12.5f));
    l->setColour (juce::Label::textColourId, C::dim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

// ---------------------------------------------------------------------------
// Small building blocks
// ---------------------------------------------------------------------------
static void styleLabel (juce::Label& l, float size, juce::Colour col = C::text, bool bold = false,
                        juce::Justification j = juce::Justification::centredLeft)
{
    l.setFont (font (size, bold));
    l.setColour (juce::Label::textColourId, col);
    l.setJustificationType (j);
    l.setInterceptsMouseClicks (false, false);
}

static void plainValueBox (juce::Slider& s)
{
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour (juce::Slider::textBoxTextColourId, C::dim);
    s.setColour (juce::Slider::textBoxHighlightColourId, C::goldDim);
}

static void sectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
{
    g.setColour (C::goldDim.brighter (0.5f));
    g.setFont (font (12.0f, true));
    g.drawText (t.toUpperCase(), r, juce::Justification::topLeft);
}

static void drawPanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (C::panel);
    g.fillRoundedRectangle (r.toFloat(), 10.0f);
    g.setColour (C::edge);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.0f, 1.0f);
}

/** Waveform display with optional playhead. */
class Waveform : public juce::Component, private juce::ChangeListener
{
public:
    Waveform() : thumb (512, formats, cache) { formats.registerBasicFormats(); thumb.addChangeListener (this); }
    ~Waveform() override { thumb.removeChangeListener (this); }
    void setFile (const juce::File& f)
    {
        if (f == current) return;
        current = f;
        if (f.existsAsFile()) thumb.setSource (new juce::FileInputSource (f));
        else thumb.clear();
        repaint();
    }
    void setPlayhead (double seconds) { if (! juce::approximatelyEqual (seconds, head)) { head = seconds; repaint(); } }
    double getLength() const { return thumb.getTotalLength(); }
    std::function<void (double)> onSeek;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        if (thumb.getTotalLength() <= 0) return;
        g.setColour (C::gold.withAlpha (0.85f));
        thumb.drawChannels (g, r.reduced (0, 4), 0.0, thumb.getTotalLength(), 1.0f);
        if (head > 0)
        {
            const int x = r.getX() + (int) (r.getWidth() * head / juce::jmax (0.001, displayLength > 0 ? displayLength : thumb.getTotalLength()));
            g.setColour (C::text);
            g.fillRect (x, r.getY(), 2, r.getHeight());
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onSeek && thumb.getTotalLength() > 0)
            onSeek (thumb.getTotalLength() * e.position.x / (float) getWidth());
    }
    double displayLength = 0;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumb;
    juce::File current;
    double head = 0;
};

/** Grab this to drag a rendered WAV straight into the DAW. */
class DragHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragHandle (AIProcessor& p, juce::String layer, juce::String text, bool big)
        : proc (p), layerId (std::move (layer)), label (std::move (text)), large (big)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (large ? "Drag the whole choir (all unmuted layers, with mic and post-FX) onto a track in your DAW."
                          : "Drag just this layer onto a track in your DAW.");
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (large ? C::gold.withAlpha (busy ? 0.6f : 1.0f) : C::panel2);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (large ? C::gold : C::edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        // grip dots
        g.setColour (large ? C::bg : C::dim);
        const float gx = r.getX() + 12, gy = r.getCentreY();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                g.fillEllipse (gx + j * 5.0f, gy - 6.0f + i * 5.0f, 2.6f, 2.6f);
        g.setFont (font (large ? 15.5f : 12.5f, large));
        g.drawText (busy ? "Preparing..." : label, r.withTrimmedLeft (26).toNearestInt(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 4) return;
        started = true;
        busy = true; repaint();
        auto f = proc.exportForDrag (layerId);
        busy = false; repaint();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }
    void mouseUp (const juce::MouseEvent&) override { started = false; }

private:
    AIProcessor& proc;
    juce::String layerId, label;
    bool large, started = false, busy = false;
};

// ---------------------------------------------------------------------------
// Screen 1: Vocal
// ---------------------------------------------------------------------------
class VocalPage : public juce::Component
{
public:
    explicit VocalPage (AIEditor& e) : ed (e)
    {
        addAndMakeVisible (wave);
        for (auto* b : { &browse, &play, &next }) addAndMakeVisible (b);
        browse.setButtonText ("Browse...");
        browse.onClick = [this] { ed.browseForVocal(); };
        play.onClick = [this] { if (ed.proc.isPlaying() && ! ed.proc.isPlayingChoir()) ed.proc.stop(); else { ed.proc.seek (0); ed.proc.play (false); } };
        next.setButtonText ("Next: Choir");
        next.getProperties().set ("primary", true);
        next.onClick = [this] { ed.showPage (1); };
        for (auto* l : { &info, &keyInfo }) { addAndMakeVisible (l); }
        styleLabel (info, 14.5f);
        styleLabel (keyInfo, 14.5f, C::gold);
        addAndMakeVisible (checks);
        styleLabel (checks, 13.5f, C::dim, false, juce::Justification::topLeft);
        wave.onSeek = [this] (double s) { ed.proc.seek (s); };
    }

    void refresh()
    {
        auto& p = ed.proc;
        wave.setFile (p.getVocalFile());
        wave.setPlayhead (p.isPlaying() && ! p.isPlayingChoir() ? p.getPlayPositionSeconds() : 0.0);
        const bool has = p.hasVocal();
        play.setEnabled (has);
        next.setEnabled (has);
        play.setButtonText (p.isPlaying() && ! p.isPlayingChoir() ? "Stop" : "Play vocal");
        if (! has) { info.setText ({}, juce::dontSendNotification); keyInfo.setText ({}, juce::dontSendNotification); checks.setText ({}, juce::dontSendNotification); return; }

        juce::String chans = p.getVocalChannels() == 1 ? "mono"
                           : (p.vocalHasDistinctSides() ? "stereo (separate left/right takes)" : "stereo (same on both sides - treated as mono)");
        info.setText (p.getVocalFile().getFileName().replace ("input", "vocal") + "   |   "
                      + juce::String (p.getVocalSeconds(), 1) + " s   |   " + chans
                      + "   |   peak " + juce::String (p.getVocalPeakDb(), 1) + " dBFS", juce::dontSendNotification);
        auto k = p.getKeyEstimate();
        keyInfo.setText (k.valid ? "Sounds like " + k.name() + ", sung range " + noteName (k.lowHz) + " to " + noteName (k.highHz)
                                 : "Listening for the key...", juce::dontSendNotification);

        juce::StringArray c;
        auto ok = [] (bool good, const juce::String& yes, const juce::String& no) { return (good ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93  ")) + yes : juce::String ("!  ") + no); };
        c.add (ok (p.getVocalSeconds() <= 45.0, "Length is fine.",
                   "Over 45 s: it works, but takes longer. Consider rendering just the parts that need a choir."));
        c.add (ok (p.getVocalPeakDb() > -24.0f && p.getVocalPeakDb() < -0.3f, "Level is good.",
                   p.getVocalPeakDb() >= -0.3f ? "The vocal is clipping or very hot. Peaks around -12 to -6 dBFS work best."
                                                : "The vocal is very quiet. Add clean gain so peaks sit around -12 to -6 dBFS."));
        c.add (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2  ")) + "Best results: one singer, dry (no reverb or delay), no doubles or harmonies, natural tuning (not hard-tuned).");
        checks.setText (c.joinIntoString ("\n"), juce::dontSendNotification);
    }

    static juce::String noteName (float hz)
    {
        if (hz <= 0) return "-";
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int m = (int) std::lround (69 + 12 * std::log2 (hz / 440.0));
        return juce::String (n[(m % 12 + 12) % 12]) + juce::String (m / 12 - 1);
    }

    void paint (juce::Graphics& g) override
    {
        const float s = ed.scale();
        drawPanel (g, dropArea);
        if (! ed.proc.hasVocal())
        {
            juce::Path dash;
            juce::Path border; border.addRoundedRectangle (dropArea.reduced ((int) (14 * s)).toFloat(), 8.0f);
            const float d[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dash, border, d, 2);
            g.setColour (hover ? C::gold : C::edge.brighter (0.3f));
            g.fillPath (dash);
            g.setColour (C::text);
            g.setFont (font (22.0f * s, true));
            auto t = dropArea.withTrimmedTop ((int) (dropArea.getHeight() * 0.28f));
            g.drawText ("Drop your lead vocal here", t.removeFromTop ((int) (34 * s)), juce::Justification::centred);
            g.setColour (C::dim);
            g.setFont (font (14.0f * s));
            g.drawText ("One singer, dry, monophonic. WAV, AIFF, FLAC or MP3.", t.removeFromTop ((int) (24 * s)), juce::Justification::centred);
        }
        sectionTitle (g, checksArea.translated (0, -(int) (20 * s)), "Before you render");
    }

    void resized() override
    {
        const float s = ed.scale();
        auto r = getLocalBounds().reduced ((int) (20 * s), (int) (10 * s));
        dropArea = r.removeFromTop ((int) (230 * s));
        wave.setBounds (dropArea.reduced ((int) (18 * s)));
        browse.setBounds (dropArea.getRight() - (int) (130 * s), dropArea.getBottom() - (int) (44 * s), (int) (112 * s), (int) (30 * s));
        r.removeFromTop ((int) (12 * s));
        auto row = r.removeFromTop ((int) (26 * s));
        play.setBounds (row.removeFromLeft ((int) (120 * s)));
        row.removeFromLeft ((int) (14 * s));
        info.setBounds (row);
        keyInfo.setBounds (r.removeFromTop ((int) (28 * s)).withTrimmedLeft ((int) (134 * s)));
        r.removeFromTop ((int) (28 * s));
        auto bottom = r.removeFromBottom ((int) (46 * s));
        next.setBounds (bottom.removeFromRight ((int) (200 * s)));
        checksArea = r;
        checks.setBounds (r);
    }

    bool hover = false;

private:
    AIEditor& ed;
    Waveform wave;
    juce::TextButton browse, play, next;
    juce::Label info, keyInfo, checks;
    juce::Rectangle<int> dropArea, checksArea;
};

// ---------------------------------------------------------------------------
// Screen 2: Choir
// ---------------------------------------------------------------------------
class ModelCard : public juce::Component
{
public:
    ModelCard (const ChoirModel& m) : model (m) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (selected ? C::gold.withAlpha (0.14f) : C::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (selected ? C::gold : C::edge);
        g.drawRoundedRectangle (r, 8.0f, selected ? 2.0f : 1.0f);
        auto t = getLocalBounds().reduced (12, 10);
        g.setColour (selected ? C::gold : C::text);
        g.setFont (font (16.0f, true));
        g.drawText (model.label, t.removeFromTop (22), juce::Justification::topLeft);
        g.setColour (C::dim);
        g.setFont (font (12.5f));
        g.drawFittedText (model.blurb, t, juce::Justification::topLeft, 5);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
    std::function<void()> onClick;
    bool selected = false;
    const ChoirModel& model;
};

class ChoirPage : public juce::Component
{
public:
    explicit ChoirPage (AIEditor& e) : ed (e)
    {
        for (auto& m : choirModels())
        {
            auto* c = cards.add (new ModelCard (m));
            c->onClick = [this, id = juce::String (m.id)] { ed.proc.settings.model = id; refresh(); };
            addAndMakeVisible (c);
        }
        for (auto& m : micSpecs()) mic.addItem (m.name, mic.getNumItems() + 1);
        addAndMakeVisible (mic);
        micAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ed.proc.apvts, "mic", mic);
        addAndMakeVisible (micDesc);
        styleLabel (micDesc, 12.5f, C::dim);
        mic.onChange = [this] { updateMicDesc(); };

        addAndMakeVisible (lead);
        lead.setButtonText ("Lead line");
        lead.setToggleState (true, juce::dontSendNotification);
        lead.setInterceptsMouseClicks (false, false);
        lead.setTooltip ("The choir always sings your melody. Add layers for a bigger arrangement.");
        addAndMakeVisible (plan);
        styleLabel (plan, 15.0f, C::text, true);
        for (size_t i = 1; i < layerDefs().size(); ++i)
        {
            auto* b = layerButtons.add (new juce::TextButton (layerDefs()[i].label));
            b->setClickingTogglesState (true);
            b->onClick = [this, id = juce::String (layerDefs()[i].id), b]
            {
                auto& L = ed.proc.settings.layers;
                if (b->getToggleState()) L.addIfNotAlreadyThere (id); else L.removeString (id);
            };
            addAndMakeVisible (b);
        }
        layerButtons[0]->setTooltip ("A section singing an octave above you. Recommended.");
        layerButtons[1]->setTooltip ("A section singing an octave below you. Recommended.");
        for (int i = 2; i < layerButtons.size(); ++i)
            layerButtons[i]->setTooltip ("Harmony that follows the key below, so it stays in tune with your song.");

        key.addItem ("Auto-detect", 1);
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int sc = 0; sc < 2; ++sc)
            for (int t = 0; t < 12; ++t)
                key.addItem (juce::String (n[t]) + (sc ? " minor" : " major"), 2 + sc * 12 + t);
        key.onChange = [this] { ed.proc.settings.key = key.getSelectedId() <= 1 ? juce::String ("auto") : key.getText(); };
        addAndMakeVisible (key);

        for (auto* b : { &stereoBtn, &monoBtn }) { b->setClickingTogglesState (true); b->setRadioGroupId (11); addAndMakeVisible (b); }
        stereoBtn.setButtonText ("Stereo"); monoBtn.setButtonText ("Mono");
        stereoBtn.onClick = [this] { ed.proc.settings.stereo = true; };
        monoBtn.onClick = [this] { ed.proc.settings.stereo = false; };
        stereoBtn.setTooltip ("Mono vocal: a wide stereo choir. Stereo vocal with separate L/R takes: each side becomes its own choir.");
        monoBtn.setTooltip ("Mono choir. With a stereo file, only the left channel is used.");

        const char* q[] = { "Draft", "Standard", "High" };
        const char* qid[] = { "draft", "standard", "high" };
        const char* qtip[] = { "8 singers per layer, fastest. Good for trying ideas.", "12 singers per layer. The best balance for most songs.", "20 singers per layer, closest voice match. Slowest." };
        for (int i = 0; i < 3; ++i)
        {
            auto* b = qualityButtons.add (new juce::TextButton (q[i]));
            b->setClickingTogglesState (true); b->setRadioGroupId (12); b->setTooltip (qtip[i]);
            b->onClick = [this, id = juce::String (qid[i])] { ed.proc.settings.quality = id; };
            addAndMakeVisible (b);
        }

        create.setButtonText ("Create Vocals");
        create.getProperties().set ("primary", true);
        create.onClick = [this] { if (ed.proc.startRender()) refresh(); };
        addAndMakeVisible (create);
        cancel.setButtonText ("Cancel");
        cancel.onClick = [this] { ed.proc.cancelRender(); };
        addChildComponent (cancel);
        addAndMakeVisible (status);
        styleLabel (status, 13.0f, C::dim);
        installBtn.setButtonText ("Install the AI engine");
        installBtn.onClick = [] { paths::setupScript().startAsProcess(); };
        addChildComponent (installBtn);
        refresh();
    }

    void updateMicDesc()
    {
        const int i = juce::jlimit (0, (int) micSpecs().size() - 1, mic.getSelectedItemIndex());
        micDesc.setText (micSpecs()[(size_t) i].description, juce::dontSendNotification);
    }

    void refresh()
    {
        auto& p = ed.proc;
        auto& st = p.settings;
        for (auto* c : cards) { const bool sel = st.model == c->model.id; if (sel != c->selected) { c->selected = sel; c->repaint(); } }
        for (int i = 0; i < layerButtons.size(); ++i)
            layerButtons[i]->setToggleState (st.layers.contains (layerDefs()[(size_t) i + 1].id), juce::dontSendNotification);
        stereoBtn.setToggleState (st.stereo, juce::dontSendNotification);
        monoBtn.setToggleState (! st.stereo, juce::dontSendNotification);
        const char* qid[] = { "draft", "standard", "high" };
        for (int i = 0; i < 3; ++i) qualityButtons[i]->setToggleState (st.quality == qid[i], juce::dontSendNotification);

        auto k = p.getKeyEstimate();
        key.changeItemText (1, k.valid ? "Auto-detect (" + k.name() + ")" : "Auto-detect");
        if (st.key == "auto") key.setSelectedId (1, juce::dontSendNotification);
        else
            for (int i = 0; i < key.getNumItems(); ++i)
                if (key.getItemText (i) == st.key) key.setSelectedItemIndex (i, juce::dontSendNotification);
        updateMicDesc();

        {
            const int nLayers = 1 + st.layers.size();
            const int perLayer = st.quality == "draft" ? 8 : st.quality == "high" ? 20 : 12;
            juce::String modelName;
            for (auto& m : choirModels()) if (st.model == m.id) modelName = m.label;
            juce::String keyText = st.key == "auto" ? (k.valid ? k.name() + " (detected)" : juce::String ("auto-detected key")) : st.key;
            const bool sides = st.stereo && p.getVocalChannels() == 2 && p.vocalHasDistinctSides();
            plan.setText (modelName + ":  " + juce::String (nLayers) + (nLayers == 1 ? " layer" : " layers") + " x "
                          + juce::String (perLayer) + " singers = " + juce::String (nLayers * perLayer * (sides ? 2 : 1)) + " voices,  "
                          + (st.stereo ? "stereo" : "mono") + ",  " + keyText, juce::dontSendNotification);
        }

        auto& r = p.getRunner();
        const auto state = r.getState();
        const bool running = state == JobRunner::State::running;
        const auto eng = paths::engineState();
        create.setEnabled (! running && p.hasVocal() && eng == paths::EngineState::ready);
        create.setButtonText (running ? "Creating..." : "Create Vocals");
        cancel.setVisible (running);
        installBtn.setVisible (eng != paths::EngineState::ready && paths::setupScript().existsAsFile());
        progress = running ? r.getProgress() : (state == JobRunner::State::done ? 1.0f : 0.0f);

        juce::String msg;
        if (eng == paths::EngineState::notInstalled)
            msg = paths::setupScript().existsAsFile() ? "The AI engine isn't set up yet. Click Install (about 6 GB, 10-30 minutes, one time)."
                                                     : "The AI engine isn't installed. Run install.command from the OpenChoir download.";
        else if (eng == paths::EngineState::voicesMissing)
            msg = "The singer voices haven't been downloaded yet. Click Install to finish setup.";
        else if (! p.hasVocal())
            msg = "Load a vocal on the Vocal screen first.";
        else if (running)
        {
            msg = r.getStatus();
            const int eta = r.getEta();
            if (eta > 0) msg += "   |   about " + (eta >= 90 ? juce::String (eta / 60) + " min" : juce::String (eta) + " s") + " left";
        }
        else if (state == JobRunner::State::failed)
            msg = "Render failed: " + r.getError();
        else if (state == JobRunner::State::cancelled)
            msg = "Cancelled.";
        else
            msg = "Rendering runs on this computer. Nothing is uploaded.";
        status.setText (msg, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, state == JobRunner::State::failed ? C::warn : C::dim);
        repaint (barArea);
    }

    void paint (juce::Graphics& g) override
    {
        const float s = ed.scale();
        drawPanel (g, modelArea); drawPanel (g, arrangeArea); drawPanel (g, renderArea);
        sectionTitle (g, modelArea.reduced ((int) (14 * s), (int) (10 * s)), "1  Choir");
        sectionTitle (g, arrangeArea.reduced ((int) (14 * s), (int) (10 * s)), "2  Arrangement");
        g.setColour (C::dim); g.setFont (font (12.5f * s));
        for (auto& [r, t] : labels) g.drawText (t, r, juce::Justification::centredLeft);
        // progress bar
        g.setColour (C::panel2);
        g.fillRoundedRectangle (barArea.toFloat(), 4.0f);
        g.setColour (C::gold);
        g.fillRoundedRectangle (barArea.toFloat().withWidth (barArea.getWidth() * progress), 4.0f);
    }

    void resized() override
    {
        const float s = ed.scale();
        auto S = [s] (float v) { return (int) (v * s); };
        auto r = getLocalBounds().reduced (S (20), S (10));
        labels.clear();

        modelArea = r.removeFromTop (S (196));
        auto m = modelArea.reduced (S (14), S (10)).withTrimmedTop (S (22));
        auto micRow = m.removeFromBottom (S (30));
        m.removeFromBottom (S (10));
        const int cw = (m.getWidth() - S (24)) / 3;
        for (int i = 0; i < cards.size(); ++i) cards[i]->setBounds (m.getX() + i * (cw + S (12)), m.getY(), cw, m.getHeight());
        labels.push_back ({ micRow.removeFromLeft (S (44)), "Mic" });
        mic.setBounds (micRow.removeFromLeft (S (190)));
        micRow.removeFromLeft (S (12));
        micDesc.setBounds (micRow);

        r.removeFromTop (S (12));
        arrangeArea = r.removeFromTop (S (166));
        auto a = arrangeArea.reduced (S (14), S (10)).withTrimmedTop (S (22));
        auto row1 = a.removeFromTop (S (32));
        labels.push_back ({ row1.removeFromLeft (S (92)), "Octaves" });
        lead.setBounds (row1.removeFromLeft (S (110))); row1.removeFromLeft (S (8));
        layerButtons[0]->setBounds (row1.removeFromLeft (S (120))); row1.removeFromLeft (S (8));
        layerButtons[1]->setBounds (row1.removeFromLeft (S (120)));
        row1.removeFromLeft (S (30));
        labels.push_back ({ row1.removeFromLeft (S (52)), "Output" });
        stereoBtn.setBounds (row1.removeFromLeft (S (80))); row1.removeFromLeft (S (6));
        monoBtn.setBounds (row1.removeFromLeft (S (80)));
        a.removeFromTop (S (10));
        auto row2 = a.removeFromTop (S (32));
        labels.push_back ({ row2.removeFromLeft (S (92)), "Harmonies" });
        for (int i = 2; i < layerButtons.size(); ++i) { layerButtons[i]->setBounds (row2.removeFromLeft (S (104))); row2.removeFromLeft (S (8)); }
        a.removeFromTop (S (10));
        auto row3 = a.removeFromTop (S (30));
        labels.push_back ({ row3.removeFromLeft (S (92)), "Key" });
        key.setBounds (row3.removeFromLeft (S (230)));
        row3.removeFromLeft (S (30));
        labels.push_back ({ row3.removeFromLeft (S (52)), "Quality" });
        for (auto* b : qualityButtons) { b->setBounds (row3.removeFromLeft (S (90))); row3.removeFromLeft (S (6)); }

        r.removeFromTop (S (12));
        renderArea = r;
        auto rr = renderArea.reduced (S (14), S (12));
        auto right = rr.removeFromRight (S (230));
        create.setBounds (right.removeFromTop (S (48)));
        cancel.setBounds (right.removeFromTop (S (34)).withTrimmedTop (S (6)).reduced (S (40), 0));
        installBtn.setBounds (cancel.getBounds());
        rr.removeFromRight (S (16));
        plan.setBounds (rr.removeFromTop (S (26)));
        rr.removeFromTop (S (10));
        barArea = rr.removeFromTop (S (10));
        rr.removeFromTop (S (8));
        status.setBounds (rr.removeFromTop (S (40)));
    }

private:
    AIEditor& ed;
    juce::OwnedArray<ModelCard> cards;
    juce::ComboBox mic, key;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> micAttach;
    juce::Label micDesc, status, plan;
    juce::TextButton lead, stereoBtn, monoBtn, create, cancel, installBtn;
    juce::OwnedArray<juce::TextButton> layerButtons, qualityButtons;
    juce::Rectangle<int> modelArea, arrangeArea, renderArea, barArea;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
    float progress = 0;
};

// ---------------------------------------------------------------------------
// Screen 3: Mix
// ---------------------------------------------------------------------------
class LayerRow : public juce::Component
{
public:
    LayerRow (AIProcessor& p, const juce::String& id, const juce::String& text)
        : drag (p, id, "drag", false)
    {
        name.setText (text, juce::dontSendNotification);
        styleLabel (name, 14.0f);
        addAndMakeVisible (name);
        mute.setButtonText ("M");
        mute.setClickingTogglesState (true);
        mute.setTooltip ("Mute this layer");
        addAndMakeVisible (mute);
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        plainValueBox (level);
        addAndMakeVisible (level);
        addAndMakeVisible (drag);
        levelA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, AIProcessor::levelParam (id), level);
        muteA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, AIProcessor::muteParam (id), mute);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        name.setBounds (r.removeFromLeft (r.getWidth() * 26 / 100));
        mute.setBounds (r.removeFromLeft (r.getHeight()).reduced (2));
        r.removeFromLeft (8);
        drag.setBounds (r.removeFromRight (74).reduced (0, 2));
        r.removeFromRight (8);
        level.setBounds (r);
    }

private:
    juce::Label name;
    juce::TextButton mute;
    juce::Slider level;
    DragHandle drag;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteA;
};

class MixPage : public juce::Component
{
public:
    explicit MixPage (AIEditor& e) : ed (e), dragAll (e.proc, {}, "Drag choir into your DAW", true)
    {
        addAndMakeVisible (wave);
        wave.onSeek = [this] (double s) { ed.proc.seek (s); if (! ed.proc.isPlaying()) ed.proc.play (true); };
        play.onClick = [this] { if (ed.proc.isPlaying() && ed.proc.isPlayingChoir()) ed.proc.stop(); else ed.proc.play (true); };
        addAndMakeVisible (play);
        addAndMakeVisible (summary);
        styleLabel (summary, 13.0f, C::dim);
        for (auto& m : micSpecs()) mic.addItem (m.name, mic.getNumItems() + 1);
        addAndMakeVisible (mic);
        micA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ed.proc.apvts, "mic", mic);
        mic.setTooltip ("Changes instantly - no re-render needed.");

        const char* ids[] = { "loCut", "brightness", "reverb", "output" };
        const char* names[] = { "Lo-Cut", "Brightness", "Reverb", "Output" };
        const char* tips[] = { "High-pass filter from 50 Hz. Turn up to thin the choir out.",
                               "High shelf around 3 kHz.",
                               "1.5 s room, from fully dry to reverb only.",
                               "Overall level." };
        for (int i = 0; i < 4; ++i)
        {
            auto* k = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow));
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
            k->setTooltip (tips[i]);
            plainValueBox (*k);
            addAndMakeVisible (k);
            knobAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (ed.proc.apvts, ids[i], *k));
            auto* l = knobLabels.add (new juce::Label ({}, names[i]));
            styleLabel (*l, 13.5f, C::text, true, juce::Justification::centred);
            addAndMakeVisible (l);
        }
        addAndMakeVisible (dragAll);
        reveal.setButtonText ("Show in Finder");
        reveal.onClick = [this] { auto f = ed.proc.exportForDrag(); if (f.existsAsFile()) f.revealToUser(); };
        report.setButtonText ("Render report");
        report.onClick = [this] { auto f = ed.proc.getReportFile(); if (f.existsAsFile()) f.startAsProcess(); };
        back.setButtonText ("Change choir");
        back.onClick = [this] { ed.showPage (1); };
        for (auto* b : { &reveal, &report, &back }) addAndMakeVisible (b);
    }

    void refresh()
    {
        auto& p = ed.proc;
        const auto ids = p.getLayerIds();
        if (ids != shownLayers) rebuildRows (ids);
        auto main = p.getSessionDir().getChildFile ("layers/main.wav");
        wave.setFile (main);
        wave.displayLength = wave.getLength();
        wave.setPlayhead (p.isPlaying() && p.isPlayingChoir() ? p.getPlayPositionSeconds() : 0.0);
        play.setButtonText (p.isPlaying() && p.isPlayingChoir() ? "Stop" : "Play choir");
        summary.setText (p.getResultSummary(), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const float s = ed.scale();
        drawPanel (g, topArea); drawPanel (g, layersArea); drawPanel (g, fxArea);
        sectionTitle (g, layersArea.reduced ((int) (14 * s), (int) (10 * s)), "Layers");
        sectionTitle (g, fxArea.reduced ((int) (14 * s), (int) (10 * s)), "Mic & Post-FX");
    }

    void resized() override
    {
        const float s = ed.scale();
        auto S = [s] (float v) { return (int) (v * s); };
        auto r = getLocalBounds().reduced (S (20), S (10));
        topArea = r.removeFromTop (S (120));
        auto t = topArea.reduced (S (14), S (10));
        auto tl = t.removeFromLeft (S (130));
        play.setBounds (tl.removeFromTop (S (34)));
        wave.setBounds (t.withTrimmedLeft (S (12)).withTrimmedBottom (S (20)));
        summary.setBounds (t.withTrimmedLeft (S (12)).removeFromBottom (S (18)));

        r.removeFromTop (S (12));
        auto bottom = r.removeFromBottom (S (52));
        r.removeFromBottom (S (12));
        layersArea = r.removeFromLeft (r.getWidth() * 56 / 100);
        r.removeFromLeft (S (12));
        fxArea = r;

        auto la = layersArea.reduced (S (14), S (10)).withTrimmedTop (S (24));
        const int rowH = juce::jmin (S (34), la.getHeight() / juce::jmax (1, rows.size()));
        for (auto* row : rows) row->setBounds (la.removeFromTop (rowH).reduced (0, S (3)));

        auto fa = fxArea.reduced (S (14), S (10)).withTrimmedTop (S (24));
        mic.setBounds (fa.removeFromTop (S (30)));
        fa.removeFromTop (S (12));
        const int kw = fa.getWidth() / 2, kh = fa.getHeight() / 2;
        for (int i = 0; i < knobs.size(); ++i)
        {
            auto cell = juce::Rectangle<int> (fa.getX() + (i % 2) * kw, fa.getY() + (i / 2) * kh, kw, kh);
            knobLabels[i]->setBounds (cell.removeFromTop (S (18)));
            knobs[i]->setBounds (cell.reduced (S (6), 0));
        }

        dragAll.setBounds (bottom.removeFromLeft (S (330)));
        bottom.removeFromLeft (S (12));
        back.setBounds (bottom.removeFromRight (S (140)).reduced (0, S (8)));
        bottom.removeFromRight (S (8));
        report.setBounds (bottom.removeFromRight (S (140)).reduced (0, S (8)));
        bottom.removeFromRight (S (8));
        reveal.setBounds (bottom.removeFromRight (S (140)).reduced (0, S (8)));
    }

private:
    void rebuildRows (const juce::StringArray& ids)
    {
        rows.clear();
        for (auto& id : ids)
        {
            const int li = layerIndex (id);
            auto* row = rows.add (new LayerRow (ed.proc, id, li >= 0 ? juce::String (layerDefs()[(size_t) li].label) : id));
            addAndMakeVisible (row);
        }
        shownLayers = ids;
        resized();
    }

    AIEditor& ed;
    Waveform wave;
    juce::TextButton play, reveal, report, back;
    juce::Label summary;
    juce::ComboBox mic;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> micA;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label> knobLabels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt;
    juce::OwnedArray<LayerRow> rows;
    juce::StringArray shownLayers;
    DragHandle dragAll;
    juce::Rectangle<int> topArea, layersArea, fxArea;
};

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------
AIEditor::AIEditor (AIProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    vocalPage = std::make_unique<VocalPage> (*this);
    choirPage = std::make_unique<ChoirPage> (*this);
    mixPage = std::make_unique<MixPage> (*this);
    addAndMakeVisible (*vocalPage);
    addChildComponent (*choirPage);
    addChildComponent (*mixPage);
    const char* names[] = { "1   Vocal", "2   Choir", "3   Mix" };
    for (int i = 0; i < 3; ++i)
    {
        tabs[(size_t) i].setButtonText (names[i]);
        tabs[(size_t) i].setRadioGroupId (1);
        tabs[(size_t) i].setClickingTogglesState (true);
        tabs[(size_t) i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (tabs[(size_t) i]);
    }
    setResizable (true, true);
    setResizeLimits (736, 512, 1380, 960);
    getConstrainer()->setFixedAspectRatio (920.0 / 640.0);
    setSize (920, 640);
    lastState = proc.getRunner().getState();
    showPage (proc.hasChoir() ? 2 : (proc.hasVocal() ? 1 : 0));
    startTimerHz (15);
}

AIEditor::~AIEditor() { setLookAndFeel (nullptr); }

bool AIEditor::pageEnabled (int i) const
{
    return i == 0 || (i == 1 && proc.hasVocal()) || (i == 2 && proc.hasChoir());
}

void AIEditor::showPage (int i)
{
    if (! pageEnabled (i)) i = 0;
    page = i;
    vocalPage->setVisible (i == 0);
    choirPage->setVisible (i == 1);
    mixPage->setVisible (i == 2);
    for (int t = 0; t < 3; ++t) tabs[(size_t) t].setToggleState (t == i, juce::dontSendNotification);
    timerCallback();
}

void AIEditor::timerCallback()
{
    proc.pollRender();
    const auto st = proc.getRunner().getState();
    if (st == JobRunner::State::done && lastState != JobRunner::State::done && proc.hasChoir())
        showPage (2);   // jump to the mix when a render finishes
    lastState = st;
    for (int t = 0; t < 3; ++t) tabs[(size_t) t].setEnabled (pageEnabled (t));
    if (page == 0) vocalPage->refresh();
    if (page == 1) choirPage->refresh();
    if (page == 2) mixPage->refresh();
}

void AIEditor::paint (juce::Graphics& g)
{
    const float s = scale();
    g.fillAll (C::bg);
    auto h = getLocalBounds().removeFromTop ((int) (62 * s)).reduced ((int) (20 * s), 0);
    g.setColour (C::gold);
    g.setFont (font (25.0f * s, true));
    g.drawText ("OpenChoir", h, juce::Justification::centredLeft);
    g.setColour (C::dim);
    g.setFont (font (12.5f * s));
    g.drawText ("one voice in, a whole choir out", h.withTrimmedLeft ((int) (148 * s)), juce::Justification::centredLeft);
    if (dragHover)
    {
        g.setColour (C::gold.withAlpha (0.12f));
        g.fillAll();
    }
}

void AIEditor::resized()
{
    const float s = scale();
    auto r = getLocalBounds();
    auto header = r.removeFromTop ((int) (62 * s)).reduced ((int) (20 * s), (int) (14 * s));
    for (int i = 2; i >= 0; --i)
    {
        tabs[(size_t) i].setBounds (header.removeFromRight ((int) (110 * s)));
        header.removeFromRight ((int) (8 * s));
    }
    for (auto* pg : { (juce::Component*) vocalPage.get(), (juce::Component*) choirPage.get(), (juce::Component*) mixPage.get() })
        pg->setBounds (r);
}

bool AIEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void AIEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
        {
            loadVocalFile (juce::File (f));
            return;
        }
}

void AIEditor::loadVocalFile (const juce::File& f)
{
    auto err = proc.loadVocal (f);
    if (err.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "OpenChoir", err);
    showPage (0);
}

void AIEditor::browseForVocal()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a lead vocal", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c)
                          {
                              auto f = c.getResult();
                              if (f.existsAsFile()) loadVocalFile (f);
                          });
}

} // namespace openchoir
