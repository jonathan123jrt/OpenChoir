#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "ChoirEngine.h"
#include "Presets.h"

namespace ParamIDs
{
    inline constexpr auto voices    = "voices";
    inline constexpr auto detune    = "detune";
    inline constexpr auto timing    = "timing";
    inline constexpr auto vibrato   = "vibrato";
    inline constexpr auto width     = "width";
    inline constexpr auto tone      = "tone";
    inline constexpr auto octDown   = "octDown";
    inline constexpr auto octUp     = "octUp";
    inline constexpr auto grit      = "grit";
    inline constexpr auto room      = "room";
    inline constexpr auto roomSize  = "roomSize";
    inline constexpr auto choirLvl  = "choirLevel";
    inline constexpr auto dryLvl    = "dryLevel";
}

class OpenChoirAudioProcessor : public juce::AudioProcessor
{
public:
    OpenChoirAudioProcessor();
    ~OpenChoirAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const std::vector<ChoirPreset>& presets();

private:
    openchoir::ChoirEngine engine;
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters lastReverb;
    juce::AudioBuffer<float> monoBuf, wetBuf;
    juce::SmoothedValue<float> choirGain, dryGain, roomMix;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OpenChoirAudioProcessor)
};
