#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "FXChain.h"
#include "JobRunner.h"
#include "KeyDetect.h"

namespace openchoir
{

/** The layers a choir can be built from (lead line always included). */
struct LayerDef { const char* id; const char* label; float defaultDb; };
inline const std::array<LayerDef, 7>& layerDefs()
{
    static const std::array<LayerDef, 7> d {{
        { "main",       "Lead",        0.0f },
        { "oct_up",     "Octave Up",  -4.0f },
        { "oct_down",   "Octave Down",-3.0f },
        { "third_up",   "3rd Above",  -5.0f },
        { "third_down", "3rd Below",  -5.0f },
        { "fifth_up",   "5th Above",  -5.0f },
        { "fifth_down", "5th Below",  -5.0f },
    }};
    return d;
}
inline int layerIndex (const juce::String& id)
{
    for (int i = 0; i < (int) layerDefs().size(); ++i)
        if (id == layerDefs()[(size_t) i].id) return i;
    return -1;
}

struct ChoirModel { const char* id; const char* label; const char* blurb; };
inline const std::array<ChoirModel, 3>& choirModels()
{
    static const std::array<ChoirModel, 3> m {{
        { "gospel", "Gospel Choir", "Men and women. The widest range and a powerful delivery. Best for melodic parts, big choruses, intros and outros." },
        { "treble", "Treble Choir", "The highest women's voices: bright and light. Great for high melodies and stereo width. (There are no children's voices in the open voice bank.)" },
        { "shout",  "Shout Choir",  "Men only, belted. Built for shouted parts and gang vocals." },
    }};
    return m;
}

/** Everything chosen on the Choir screen (not automatable; saved with the project). */
struct ChoirSettings
{
    juce::String model = "gospel";
    juce::StringArray layers { "oct_up", "oct_down" };   // besides the lead line
    juce::String key = "auto";                            // "auto" or e.g. "A minor"
    bool stereo = true;
    juce::String quality = "standard";                    // draft | standard | high
};

class AIProcessor : public juce::AudioProcessor
{
public:
    AIProcessor();
    ~AIProcessor() override;

    // --- AudioProcessor -----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Screen 1: vocal ----------------------------------------------------
    /** Loads a vocal; returns an error message or empty on success. */
    juce::String loadVocal (const juce::File& file);
    juce::File getVocalFile() const { return vocalFile; }
    bool hasVocal() const { return vocalFile.existsAsFile(); }
    double getVocalSeconds() const { return vocalSeconds; }
    int getVocalChannels() const { return vocalChannels; }
    bool vocalHasDistinctSides() const { return vocalDistinctSides; }
    float getVocalPeakDb() const { return vocalPeakDb; }
    KeyEstimate getKeyEstimate() const { const juce::ScopedLock sl (keyLock); return keyEstimate; }

    // --- Screen 2: choir ----------------------------------------------------
    ChoirSettings settings;
    bool startRender();
    void cancelRender() { runner.cancel(); }
    JobRunner& getRunner() { return runner; }
    /** Called by the editor's timer: picks up a finished render. */
    void pollRender();

    // --- Screen 3: mix ------------------------------------------------------
    struct Layer { juce::String id, label; juce::AudioBuffer<float> audio; };   // 44.1 kHz stereo
    bool hasChoir() const;
    juce::StringArray getLayerIds() const;
    juce::String getResultSummary() const { return resultSummary; }
    juce::File getSessionDir() const { return sessionDir; }
    juce::File getReportFile() const { return sessionDir.getChildFile ("report.txt"); }

    void play (bool choir);       // choir = rendered layers, otherwise the original vocal
    void stop();
    bool isPlaying() const { return playing.load(); }
    bool isPlayingChoir() const { return playChoir.load(); }
    double getPlayPositionSeconds() const;
    double getPlayLengthSeconds() const;
    void seek (double seconds);

    /** Bakes the current mix (levels, mutes, mic, post-FX) to a WAV for dragging
        into the DAW. layerId empty = every unmuted layer together. */
    juce::File exportForDrag (const juce::String& layerId = {});

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String levelParam (const juce::String& id) { return "lvl_" + id; }
    static juce::String muteParam (const juce::String& id)  { return "mute_" + id; }

private:
    struct LayerSet
    {
        std::vector<Layer> layers;          // original 44.1 kHz
        std::vector<juce::AudioBuffer<float>> host;   // resampled to the host rate
        double hostRate = 44100.0;
        int length = 0;                     // host-rate samples
    };

    bool loadResult (const juce::File& resultJson);
    void rebuildHostCopies (double hostRate);
    void swapLayerSet (std::shared_ptr<LayerSet> next);
    static void resampleTo (const juce::AudioBuffer<float>& in, double inRate, juce::AudioBuffer<float>& out, double outRate);
    FXParams currentFX() const;
    void mixLayersInto (const LayerSet& set, juce::AudioBuffer<float>& out, int start, int num,
                        bool useHost, std::array<float, 7>& lastGains, const juce::String& only);

    // vocal
    juce::File vocalFile, sessionDir;
    double vocalSeconds = 0; int vocalChannels = 0; bool vocalDistinctSides = false; float vocalPeakDb = -100;
    juce::AudioBuffer<float> vocalHost;   // for preview, host rate stereo
    juce::AudioBuffer<float> vocalOrig; double vocalOrigRate = 44100.0;
    KeyEstimate keyEstimate; juce::CriticalSection keyLock;
    std::unique_ptr<juce::Thread> keyThread;

    // render
    JobRunner runner;
    juce::String resultSummary;

    // playback
    std::shared_ptr<LayerSet> layerSet;            // swapped under setLock
    juce::SpinLock setLock, vocalLock;
    std::atomic<bool> playing { false }, playChoir { true };
    std::atomic<juce::int64> playPos { 0 };
    std::array<float, 7> liveGains {};
    std::array<std::atomic<float>*, 7> levelPtr {}, mutePtr {};
    std::atomic<float> *micPtr = nullptr, *loCutPtr = nullptr, *brightPtr = nullptr, *verbPtr = nullptr, *outPtr = nullptr;
    std::vector<std::shared_ptr<LayerSet>> retired;   // old layer sets, freed on the message thread
    FXChain liveFX;
    double hostRate = 44100.0;
    int hostBlock = 512;
    juce::AudioBuffer<float> mixBuf;
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIProcessor)
};

} // namespace openchoir
