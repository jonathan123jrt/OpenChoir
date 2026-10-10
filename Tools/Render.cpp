// Offline renderer: runs the OpenChoir engine over a WAV file.
//   openchoir_render <in.wav> <out.wav> [preset-index] [param=value ...]
//   params: voices detune timing vibrato width tone octdown octup grit room roomsize choir dry
// Mirrors OpenChoirAudioProcessor::processBlock so you can audition presets
// (or test changes to the DSP) without a DAW.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "ChoirEngine.h"
#include "Presets.h"
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: openchoir_render in.wav out.wav [preset-index]\n";
        for (size_t i = 0; i < openchoir::factoryPresets().size(); ++i)
            std::cerr << "  " << i << ": " << openchoir::factoryPresets()[i].name << "\n";
        return 1;
    }

    const auto& presets = openchoir::factoryPresets();
    const int presetIdx = argc > 3 ? juce::jlimit (0, (int) presets.size() - 1, std::atoi (argv[3])) : 0;
    ChoirPreset pr = presets[(size_t) presetIdx];
    for (int a = 4; a < argc; ++a)
    {
        juce::String kv (argv[a]);
        auto k = kv.upToFirstOccurrenceOf ("=", false, false).toLowerCase();
        float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (k == "voices") pr.voices = v;      else if (k == "detune") pr.detune = v;
        else if (k == "timing") pr.timing = v; else if (k == "vibrato") pr.vibrato = v;
        else if (k == "width") pr.width = v;   else if (k == "tone") pr.tone = v;
        else if (k == "octdown") pr.octDown = v; else if (k == "octup") pr.octUp = v;
        else if (k == "grit") pr.grit = v;     else if (k == "room") pr.room = v;
        else if (k == "roomsize") pr.roomSize = v; else if (k == "choir") pr.choir = v;
        else if (k == "dry") pr.dry = v;
        else { std::cerr << "unknown param " << k << "\n"; return 1; }
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])));
    if (reader == nullptr) { std::cerr << "cannot read " << argv[1] << "\n"; return 1; }

    const double sr = reader->sampleRate;
    const int tail = (int) (sr * 2.5);
    const int len  = (int) reader->lengthInSamples + tail;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    juce::AudioBuffer<float> io (2, len);
    io.clear();
    reader->read (&io, 0, (int) reader->lengthInSamples, 0, true, true);
    if (reader->numChannels == 1)
        io.copyFrom (1, 0, io, 0, 0, len);

    const int block = 512;
    openchoir::ChoirEngine engine;
    engine.prepare (sr);
    juce::dsp::Reverb reverb;
    reverb.prepare ({ sr, (juce::uint32) block, 2 });

    openchoir::EngineParams ep;
    ep.unisonVoices = (int) pr.voices;
    ep.detuneCents  = pr.detune;
    ep.timingMs     = pr.timing;
    ep.vibrato      = pr.vibrato * 0.01f;
    ep.width        = pr.width * 0.01f;
    ep.toneHz       = pr.tone;
    ep.octDownLevel = pr.octDown * 0.01f;
    ep.octUpLevel   = pr.octUp * 0.01f;
    ep.grit         = pr.grit * 0.01f;

    const float room = pr.room * 0.01f;
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.35f + 0.6f * pr.roomSize * 0.01f;
    rp.damping  = 0.45f;
    rp.width    = 1.0f;
    rp.wetLevel = room * 0.9f;
    rp.dryLevel = 1.0f - room * 0.55f;
    reverb.setParameters (rp);

    auto toGain = [] (float db) { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (db); };
    const float cg = toGain (pr.choir), dg = toGain (pr.dry);

    juce::AudioBuffer<float> mono (1, block), wet (2, block);

    for (int start = 0; start < len; start += block)
    {
        const int n = juce::jmin (block, len - start);
        auto* m  = mono.getWritePointer (0);
        auto* l  = io.getWritePointer (0, start);
        auto* r  = io.getWritePointer (1, start);
        for (int i = 0; i < n; ++i) m[i] = 0.5f * (l[i] + r[i]);

        engine.process (m, wet.getWritePointer (0), wet.getWritePointer (1), n, ep);
        juce::dsp::AudioBlock<float> ab (wet.getArrayOfWritePointers(), 2, (size_t) n);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (ab));

        for (int i = 0; i < n; ++i)
        {
            l[i] = l[i] * dg + wet.getSample (0, i) * cg;
            r[i] = r[i] * dg + wet.getSample (1, i) * cg;
        }
    }

    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::cout << "cpu=" << (ms / 1000.0) / (len / sr) * 100.0 << "% of one core (incl. reverb)\n";
    // report + guard
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < len; ++i)
            if (! std::isfinite (io.getSample (ch, i))) finite = false;
    const float peak = io.getMagnitude (0, len);
    std::cout << "preset=" << pr.name << " peak=" << juce::Decibels::gainToDecibels (peak) << "dBFS"
              << " rms=" << juce::Decibels::gainToDecibels (0.5f * (io.getRMSLevel (0, 0, len) + io.getRMSLevel (1, 0, len))) << "dBFS"
              << " finite=" << (finite ? "yes" : "NO") << "\n";
    if (peak > 0.98f) io.applyGain (0.98f / peak);

    auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (outFile), sr, 2, 24, {}, 0));
    if (writer == nullptr) { std::cerr << "cannot write " << argv[2] << "\n"; return 1; }
    writer->writeFromAudioSampleBuffer (io, 0, len);
    return finite ? 0 : 2;
}
