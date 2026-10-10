#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
const std::vector<ChoirPreset>& OpenChoirAudioProcessor::presets() { return openchoir::factoryPresets(); }

APVTS::ParameterLayout OpenChoirAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto db  = [] (float v, int) { return v <= -59.9f ? String ("Off") : String (v, 1) + " dB"; };
    auto hz  = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };

    p.push_back (std::make_unique<AudioParameterInt>   (ParameterID { ParamIDs::voices, 1 }, "Voices", 1, 16, 12));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::detune, 1 }, "Detune",
                    NormalisableRange<float> (0.0f, 40.0f, 0.1f), 14.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ct"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::timing, 1 }, "Timing",
                    NormalisableRange<float> (0.0f, 90.0f, 0.1f, 0.7f), 30.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::vibrato, 1 }, "Vibrato",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 45.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::width, 1 }, "Width",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 85.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tone, 1 }, "Tone",
                    NormalisableRange<float> (1500.0f, 18000.0f, 1.0f, 0.4f), 8000.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::octDown, 1 }, "Octave Down",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::octUp, 1 }, "Octave Up",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::grit, 1 }, "Grit",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::room, 1 }, "Room",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::roomSize, 1 }, "Room Size",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::choirLvl, 1 }, "Choir Level",
                    NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::dryLvl, 1 }, "Dry Level",
                    NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.0f), -6.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction (db)));

    return { p.begin(), p.end() };
}

//==============================================================================
OpenChoirAudioProcessor::OpenChoirAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OpenChoir", createLayout())
{
}

bool OpenChoirAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void OpenChoirAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    engine.reset();

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    reverb.prepare (spec);
    reverb.reset();
    lastReverb = {};
    lastReverb.roomSize = -1.0f;  // force the first update

    monoBuf.setSize (1, samplesPerBlock);
    wetBuf.setSize (2, samplesPerBlock);

    for (auto* s : { &choirGain, &dryGain, &roomMix })
        s->reset (sampleRate, 0.05);
}

static float dbToGainOrZero (float db)
{
    return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db);
}

void OpenChoirAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn      = getTotalNumInputChannels();

    if (monoBuf.getNumSamples() < numSamples)
    {
        monoBuf.setSize (1, numSamples, false, false, true);
        wetBuf.setSize (2, numSamples, false, false, true);
    }

    // mono input -> stereo output: duplicate so the dry path is centred
    if (numIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // --- parameters --------------------------------------------------------
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    openchoir::EngineParams ep;
    ep.unisonVoices = (int) get (ParamIDs::voices);
    ep.detuneCents  = get (ParamIDs::detune);
    ep.timingMs     = get (ParamIDs::timing);
    ep.vibrato      = get (ParamIDs::vibrato) * 0.01f;
    ep.width        = get (ParamIDs::width)   * 0.01f;
    ep.toneHz       = get (ParamIDs::tone);
    ep.octDownLevel = get (ParamIDs::octDown) * 0.01f;
    ep.octUpLevel   = get (ParamIDs::octUp)   * 0.01f;
    ep.grit         = get (ParamIDs::grit)    * 0.01f;

    choirGain.setTargetValue (dbToGainOrZero (get (ParamIDs::choirLvl)));
    dryGain.setTargetValue   (dbToGainOrZero (get (ParamIDs::dryLvl)));

    const float room = get (ParamIDs::room) * 0.01f;
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize   = 0.35f + 0.6f * get (ParamIDs::roomSize) * 0.01f;
    rp.damping    = 0.45f;
    rp.width      = 1.0f;
    rp.wetLevel   = room * 0.9f;
    rp.dryLevel   = 1.0f - room * 0.55f;
    rp.freezeMode = 0.0f;
    // only touch the reverb when something changed (setParameters recalculates its filters)
    if (! juce::exactlyEqual (rp.roomSize, lastReverb.roomSize) || ! juce::exactlyEqual (rp.wetLevel, lastReverb.wetLevel)
        || ! juce::exactlyEqual (rp.dryLevel, lastReverb.dryLevel))
    {
        reverb.setParameters (rp);
        lastReverb = rp;
    }

    // --- mono sum ----------------------------------------------------------
    auto* mono = monoBuf.getWritePointer (0);
    const auto* inL = buffer.getReadPointer (0);
    const auto* inR = buffer.getReadPointer (1);
    for (int i = 0; i < numSamples; ++i)
        mono[i] = 0.5f * (inL[i] + inR[i]);

    // --- ensemble ----------------------------------------------------------
    auto* wL = wetBuf.getWritePointer (0);
    auto* wR = wetBuf.getWritePointer (1);
    engine.process (mono, wL, wR, numSamples, ep);

    {
        juce::dsp::AudioBlock<float> block (wetBuf.getArrayOfWritePointers(), 2, (size_t) numSamples);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // --- mix ---------------------------------------------------------------
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float cg = choirGain.getNextValue();
        const float dg = dryGain.getNextValue();
        outL[i] = outL[i] * dg + wL[i] * cg;
        outR[i] = outR[i] * dg + wR[i] * cg;
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
int OpenChoirAudioProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String OpenChoirAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, (int) presets().size()))
        return presets()[(size_t) index].name;
    return {};
}

void OpenChoirAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;

    currentProgram = index;
    const auto& pr = presets()[(size_t) index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (ParamIDs::voices,   pr.voices);
    set (ParamIDs::detune,   pr.detune);
    set (ParamIDs::timing,   pr.timing);
    set (ParamIDs::vibrato,  pr.vibrato);
    set (ParamIDs::width,    pr.width);
    set (ParamIDs::tone,     pr.tone);
    set (ParamIDs::octDown,  pr.octDown);
    set (ParamIDs::octUp,    pr.octUp);
    set (ParamIDs::grit,     pr.grit);
    set (ParamIDs::room,     pr.room);
    set (ParamIDs::roomSize, pr.roomSize);
    set (ParamIDs::choirLvl, pr.choir);
    set (ParamIDs::dryLvl,   pr.dry);
}

void OpenChoirAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OpenChoirAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = tree.getProperty ("program", 0);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* OpenChoirAudioProcessor::createEditor()
{
    return new OpenChoirAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OpenChoirAudioProcessor();
}
