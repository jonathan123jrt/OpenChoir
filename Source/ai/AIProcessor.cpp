#include "AIProcessor.h"
#include "AIEditor.h"

namespace openchoir
{

static constexpr double kLayerRate = 44100.0;
static constexpr double kTailSeconds = 2.0;   // reverb ring-out after the last note

juce::AudioProcessorValueTreeState::ParameterLayout AIProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    StringArray mics;
    for (auto& m : micSpecs()) mics.add (m.name);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mic", 1 }, "Mic", mics, 0));

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + " %"; });
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "loCut", 1 }, "Lo-Cut", NormalisableRange<float> (0, 1), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (FXChain::loCutHz (v))) + " Hz"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "brightness", 1 }, "Brightness", NormalisableRange<float> (0, 1), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "+" + String (FXChain::brightnessDb (v), 1) + " dB"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "reverb", 1 }, "Reverb", NormalisableRange<float> (0, 1), 0.25f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24, 6, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    for (auto& l : layerDefs())
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { levelParam (l.id), 1 }, String (l.label) + " Level",
            NormalisableRange<float> (-60, 6, 0.1f, 2.5f), l.defaultDb,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; })));
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { muteParam (l.id), 1 }, String (l.label) + " Mute", false));
    }
    return { p.begin(), p.end() };
}

AIProcessor::AIProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OpenChoirAI", createLayout())
{
    formats.registerBasicFormats();
    for (size_t i = 0; i < layerDefs().size(); ++i)
    {
        levelPtr[i] = apvts.getRawParameterValue (levelParam (layerDefs()[i].id));
        mutePtr[i]  = apvts.getRawParameterValue (muteParam (layerDefs()[i].id));
    }
    micPtr = apvts.getRawParameterValue ("mic");
    loCutPtr = apvts.getRawParameterValue ("loCut");
    brightPtr = apvts.getRawParameterValue ("brightness");
    verbPtr = apvts.getRawParameterValue ("reverb");
    outPtr = apvts.getRawParameterValue ("output");
}

void AIProcessor::swapLayerSet (std::shared_ptr<LayerSet> next)
{
    std::shared_ptr<LayerSet> old;
    {
        const juce::SpinLock::ScopedLockType sl (setLock);
        old = layerSet;
        layerSet = std::move (next);
    }
    if (old) retired.push_back (std::move (old));   // freed later, never on the audio thread
}

AIProcessor::~AIProcessor()
{
    runner.cancel();
    if (keyThread) keyThread->stopThread (2000);
}

bool AIProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AIProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    hostBlock = samplesPerBlock;
    liveFX.prepare (sampleRate, samplesPerBlock);
    mixBuf.setSize (2, samplesPerBlock);
    liveGains.fill (0.0f);
    rebuildHostCopies (sampleRate);
    if (vocalOrig.getNumSamples() > 0)
    {
        juce::AudioBuffer<float> v;
        resampleTo (vocalOrig, vocalOrigRate, v, sampleRate);
        const juce::SpinLock::ScopedLockType sl (vocalLock);
        vocalHost = std::move (v);
    }
}

// ---------------------------------------------------------------------------
// Resampling (whole buffers, used when loading - never on the audio thread)
// ---------------------------------------------------------------------------
void AIProcessor::resampleTo (const juce::AudioBuffer<float>& in, double inRate, juce::AudioBuffer<float>& out, double outRate)
{
    const int nIn = in.getNumSamples();
    if (juce::approximatelyEqual (inRate, outRate))
    {
        out.makeCopyOf (in);
        return;
    }
    const double ratio = inRate / outRate;
    const int nOut = (int) std::ceil (nIn / ratio);
    out.setSize (in.getNumChannels(), nOut);
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        juce::WindowedSincInterpolator interp;
        // pad so the interpolator can run to the end
        std::vector<float> src ((size_t) nIn + 256, 0.0f);
        std::copy (in.getReadPointer (ch), in.getReadPointer (ch) + nIn, src.begin());
        interp.process (ratio, src.data(), out.getWritePointer (ch), nOut);
    }
}

void AIProcessor::rebuildHostCopies (double rate)
{
    std::shared_ptr<LayerSet> cur;
    { const juce::SpinLock::ScopedLockType sl (setLock); cur = layerSet; }
    if (cur == nullptr) return;
    auto next = std::make_shared<LayerSet>();
    next->layers = cur->layers;
    next->hostRate = rate;
    for (auto& l : next->layers)
    {
        juce::AudioBuffer<float> h;
        resampleTo (l.audio, kLayerRate, h, rate);
        next->length = juce::jmax (next->length, h.getNumSamples());
        next->host.push_back (std::move (h));
    }
    swapLayerSet (next);
}

// ---------------------------------------------------------------------------
// Screen 1
// ---------------------------------------------------------------------------
juce::String AIProcessor::loadVocal (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "That file can't be read. Use a WAV, AIFF, FLAC or MP3.";
    if (reader->numChannels > 2)
        return "Use a mono or stereo file.";
    if (reader->lengthInSamples < (juce::int64) (reader->sampleRate * 0.5))
        return "That file is too short.";
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 600))
        return "That file is over 10 minutes. Export just the part that needs the choir.";

    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, reader->numChannels > 1);

    stop();
    // keep a private copy with the session so the project still works if the original moves
    sessionDir = paths::newSessionDir (file.getFileNameWithoutExtension());
    auto copy = sessionDir.getChildFile ("input" + file.getFileExtension().toLowerCase());
    file.copyFileTo (copy);
    vocalFile = copy;

    vocalOrigRate = reader->sampleRate;
    vocalSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    vocalChannels = (int) reader->numChannels;
    vocalPeakDb = juce::Decibels::gainToDecibels (buf.getMagnitude (0, buf.getNumSamples()), -100.0f);
    vocalDistinctSides = false;
    if (vocalChannels == 2)
    {
        double sxx = 0, syy = 0, sxy = 0;
        auto* l = buf.getReadPointer (0); auto* r = buf.getReadPointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i) { sxx += l[i] * l[i]; syy += r[i] * r[i]; sxy += l[i] * r[i]; }
        vocalDistinctSides = sxx > 0 && syy > 0 && sxy / std::sqrt (sxx * syy) < 0.98;
    }

    // stereo preview copy
    juce::AudioBuffer<float> st (2, buf.getNumSamples());
    st.copyFrom (0, 0, buf, 0, 0, buf.getNumSamples());
    st.copyFrom (1, 0, buf, vocalChannels > 1 ? 1 : 0, 0, buf.getNumSamples());
    vocalOrig = st;
    juce::AudioBuffer<float> v;
    resampleTo (st, vocalOrigRate, v, hostRate);
    { const juce::SpinLock::ScopedLockType sl (vocalLock); vocalHost = std::move (v); }

    // a new vocal clears the old choir
    swapLayerSet (nullptr);
    resultSummary.clear();

    // estimate the key in the background
    { const juce::ScopedLock sl (keyLock); keyEstimate = {}; }
    if (keyThread) keyThread->stopThread (2000);
    struct KeyJob : juce::Thread
    {
        KeyJob (AIProcessor& o, juce::AudioBuffer<float> b, double r) : juce::Thread ("OpenChoir key"), owner (o), audio (std::move (b)), rate (r) {}
        void run() override
        {
            auto k = estimateKey (audio, rate);
            if (threadShouldExit()) return;
            const juce::ScopedLock sl (owner.keyLock);
            owner.keyEstimate = k;
        }
        AIProcessor& owner; juce::AudioBuffer<float> audio; double rate;
    };
    keyThread = std::make_unique<KeyJob> (*this, buf, reader->sampleRate);
    keyThread->startThread();
    return {};
}

// ---------------------------------------------------------------------------
// Screen 2
// ---------------------------------------------------------------------------
bool AIProcessor::startRender()
{
    if (! hasVocal() || runner.getState() == JobRunner::State::running)
        return false;
    juce::DynamicObject::Ptr spec = new juce::DynamicObject();
    spec->setProperty ("input", vocalFile.getFullPathName());
    spec->setProperty ("out_dir", sessionDir.getFullPathName());
    spec->setProperty ("model", settings.model);
    juce::Array<juce::var> layers;
    layers.add ("main");
    for (auto& l : settings.layers) layers.add (l);
    spec->setProperty ("layers", layers);
    spec->setProperty ("key", settings.key);
    spec->setProperty ("stereo", settings.stereo);
    spec->setProperty ("quality", settings.quality);
    auto engine = juce::SystemStats::getEnvironmentVariable ("OPENCHOIR_ENGINE", "auto");
    spec->setProperty ("engine", engine);
    return runner.start (juce::var (spec.get()), sessionDir);
}

void AIProcessor::pollRender()
{
    if (! retired.empty() && ! playing.load()) retired.clear();
    if (runner.getState() == JobRunner::State::done)
    {
        auto f = runner.getResultFile();
        if (f.existsAsFile() && (! hasChoir() || resultSummary.isEmpty()))
            loadResult (f);
    }
}

bool AIProcessor::loadResult (const juce::File& resultJson)
{
    auto json = juce::JSON::parse (resultJson);
    if (! json.isObject()) return false;
    auto set = std::make_shared<LayerSet>();
    for (auto& l : *json["layers"].getArray())
    {
        juce::File f (l["file"].toString());
        if (! f.existsAsFile())   // session folder moved: look next to result.json
            f = resultJson.getSiblingFile ("layers").getChildFile (l["id"].toString() + ".wav");
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) continue;
        Layer layer;
        layer.id = l["id"].toString();
        layer.label = l["label"].toString();
        layer.audio.setSize (2, (int) r->lengthInSamples);
        r->read (&layer.audio, 0, (int) r->lengthInSamples, 0, true, true);
        if (! juce::approximatelyEqual (r->sampleRate, kLayerRate))
        {
            juce::AudioBuffer<float> rs;
            resampleTo (layer.audio, r->sampleRate, rs, kLayerRate);
            layer.audio = std::move (rs);
        }
        set->layers.push_back (std::move (layer));
    }
    if (set->layers.empty()) return false;
    sessionDir = resultJson.getParentDirectory();
    resultSummary = json["model_label"].toString() + "  |  key " + json["key"].toString()
                  + "  |  " + juce::String ((int) json["voices_per_layer"]) + " singers per layer";
    swapLayerSet (set);
    rebuildHostCopies (hostRate);
    return true;
}

// ---------------------------------------------------------------------------
// Screen 3
// ---------------------------------------------------------------------------
bool AIProcessor::hasChoir() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (setLock));
    return layerSet != nullptr && ! layerSet->layers.empty();
}

juce::StringArray AIProcessor::getLayerIds() const
{
    juce::StringArray ids;
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (setLock));
    if (layerSet != nullptr)
        for (auto& l : layerSet->layers) ids.add (l.id);
    return ids;
}

FXParams AIProcessor::currentFX() const
{
    FXParams p;
    p.mic        = (int) micPtr->load();
    p.loCut      = loCutPtr->load();
    p.brightness = brightPtr->load();
    p.reverb     = verbPtr->load();
    p.outputDb   = outPtr->load();
    return p;
}

void AIProcessor::play (bool choir)
{
    if (choir && ! hasChoir()) return;
    playChoir = choir;
    if (playPos.load() >= (juce::int64) (getPlayLengthSeconds() * hostRate)) playPos = 0;
    liveFX.reset();
    playing = true;
}

void AIProcessor::stop() { playing = false; }

double AIProcessor::getPlayLengthSeconds() const
{
    if (playChoir.load())
    {
        const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (setLock));
        return layerSet ? layerSet->length / hostRate + kTailSeconds : 0.0;
    }
    return vocalSeconds;
}

double AIProcessor::getPlayPositionSeconds() const { return (double) playPos.load() / hostRate; }
void AIProcessor::seek (double s) { playPos = (juce::int64) (juce::jmax (0.0, s) * hostRate); }

void AIProcessor::mixLayersInto (const LayerSet& set, juce::AudioBuffer<float>& out, int start, int num,
                                 bool useHost, std::array<float, 7>& lastGains, const juce::String& only)
{
    out.clear (0, num);
    for (size_t i = 0; i < set.layers.size(); ++i)
    {
        const auto& L = set.layers[i];
        const int li = layerIndex (L.id);
        if (li < 0) continue;
        const bool muted = mutePtr[(size_t) li]->load() > 0.5f;
        const float db = levelPtr[(size_t) li]->load();
        float target = (muted || db <= -59.9f) ? 0.0f : juce::Decibels::decibelsToGain (db);
        if (only.isNotEmpty()) target = (L.id == only) ? juce::Decibels::decibelsToGain (juce::jmax (db, -59.9f)) : 0.0f;
        const auto& src = useHost ? set.host[i] : L.audio;
        const int avail = juce::jmin (num, src.getNumSamples() - start);
        if (avail > 0 && (target > 0 || lastGains[(size_t) li] > 0))
            for (int ch = 0; ch < 2; ++ch)
                out.addFromWithRamp (ch, 0, src.getReadPointer (ch, start), avail, lastGains[(size_t) li], target);
        lastGains[(size_t) li] = target;
    }
}

void AIProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    midi.clear();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! playing.load()) return;
    const auto pos = playPos.load();

    if (! playChoir.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (vocalLock);
        if (! sl.isLocked()) return;
        const int avail = juce::jmin ((juce::int64) n, (juce::int64) vocalHost.getNumSamples() - pos) > 0
                            ? (int) juce::jmin ((juce::int64) n, (juce::int64) vocalHost.getNumSamples() - pos) : 0;
        for (int ch = 0; ch < 2 && avail > 0; ++ch)
            buffer.copyFrom (ch, 0, vocalHost, ch, (int) pos, avail);
        playPos = pos + n;
        if (avail < n) { playing = false; playPos = 0; }
        return;
    }

    std::shared_ptr<LayerSet> set;
    {
        const juce::SpinLock::ScopedTryLockType sl (setLock);
        if (! sl.isLocked()) return;
        set = layerSet;
    }
    if (set == nullptr || set->host.size() != set->layers.size()) return;

    // work in chunks no bigger than prepareToPlay promised (some hosts send more)
    const int chunk = juce::jmax (1, mixBuf.getNumSamples());
    const auto params = currentFX();
    for (int done = 0; done < n; done += chunk)
    {
        const int m = juce::jmin (chunk, n - done);
        mixLayersInto (*set, mixBuf, (int) (pos + done), m, true, liveGains, {});
        liveFX.process (mixBuf, m, params);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom (ch, done, mixBuf, ch, 0, m);
    }

    playPos = pos + n;
    if (pos + n > set->length + (juce::int64) (kTailSeconds * hostRate)) { playing = false; playPos = 0; }
}

juce::File AIProcessor::exportForDrag (const juce::String& layerId)
{
    std::shared_ptr<LayerSet> set;
    { const juce::SpinLock::ScopedLockType sl (setLock); set = layerSet; }
    if (set == nullptr) return {};

    int len = 0;
    for (auto& l : set->layers) len = juce::jmax (len, l.audio.getNumSamples());
    const int total = len + (int) (kTailSeconds * kLayerRate) + FXChain::convBlock;
    const int block = 4096;

    FXChain fx;
    fx.prepare (kLayerRate, block);
    auto params = currentFX();
    std::array<float, 7> gains {};
    // start at full gain so there is no fade-in on the first block
    for (auto& l : set->layers)
    {
        const int li = layerIndex (l.id);
        if (li < 0) continue;
        const bool muted = mutePtr[(size_t) li]->load() > 0.5f;
        const float db = levelPtr[(size_t) li]->load();
        gains[(size_t) li] = layerId.isNotEmpty() ? (l.id == layerId ? juce::Decibels::decibelsToGain (juce::jmax (db, -59.9f)) : 0.0f)
                                                  : ((muted || db <= -59.9f) ? 0.0f : juce::Decibels::decibelsToGain (db));
    }

    juce::AudioBuffer<float> out (2, total), blk (2, block);
    for (int s = 0; s < total; s += block)
    {
        const int n = juce::jmin (block, total - s);
        mixLayersInto (*set, blk, s, n, false, gains, layerId);
        fx.process (blk, n, params);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, s, blk, ch, 0, n);
    }
    // the dry path has no delay, so the export lines up with the vocal sample-for-sample

    // trim trailing silence (keep the reverb tail)
    int end = total;
    while (end > len && out.getMagnitude (end - 1, 1) < 1.0e-5f) --end;

    auto name = juce::String ("OpenChoir ") + (layerId.isEmpty() ? juce::String ("Choir")
                                               : juce::String (layerDefs()[(size_t) juce::jmax (0, layerIndex (layerId))].label));
    auto dir = sessionDir.getChildFile ("export");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), kLayerRate, 2, 24, {}, 0));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (out, 0, end);
    return file;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void AIProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree s ("settings");
    s.setProperty ("model", settings.model, nullptr);
    s.setProperty ("layers", settings.layers.joinIntoString (","), nullptr);
    s.setProperty ("key", settings.key, nullptr);
    s.setProperty ("stereo", settings.stereo, nullptr);
    s.setProperty ("quality", settings.quality, nullptr);
    s.setProperty ("vocal", vocalFile.getFullPathName(), nullptr);
    s.setProperty ("session", sessionDir.getFullPathName(), nullptr);
    state.removeChild (state.getChildWithName ("settings"), nullptr);
    state.appendChild (s, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void AIProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    auto s = tree.getChildWithName ("settings");
    if (s.isValid())
    {
        settings.model   = s.getProperty ("model", "gospel").toString();
        settings.layers  = juce::StringArray::fromTokens (s.getProperty ("layers", "").toString(), ",", "");
        settings.layers.removeEmptyStrings();
        settings.key     = s.getProperty ("key", "auto").toString();
        settings.stereo  = (bool) s.getProperty ("stereo", true);
        settings.quality = s.getProperty ("quality", "standard").toString();
        juce::File vocal (s.getProperty ("vocal").toString());
        juce::File session (s.getProperty ("session").toString());
        tree.removeChild (s, nullptr);
        apvts.replaceState (tree);

        if (vocal.existsAsFile())
        {
            // reload the vocal without making a new session
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (vocal));
            if (r != nullptr)
            {
                vocalFile = vocal; sessionDir = session;
                vocalOrigRate = r->sampleRate; vocalSeconds = (double) r->lengthInSamples / r->sampleRate;
                vocalChannels = (int) r->numChannels;
                juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
                vocalPeakDb = juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -100.0f);
                vocalOrig = b;
                juce::AudioBuffer<float> v; resampleTo (b, vocalOrigRate, v, hostRate);
                { const juce::SpinLock::ScopedLockType sl (vocalLock); vocalHost = std::move (v); }
                const juce::ScopedLock sl (keyLock);
                keyEstimate = estimateKey (b, vocalOrigRate);
            }
        }
        auto result = session.getChildFile ("result.json");
        if (result.existsAsFile()) loadResult (result);
    }
    else
        apvts.replaceState (tree);
}

juce::AudioProcessorEditor* AIProcessor::createEditor() { return new AIEditor (*this); }

} // namespace openchoir

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new openchoir::AIProcessor(); }
