// End-to-end test of the OpenChoir instrument without a DAW:
//   load vocal -> key estimate -> render (stand-in engine) -> layers -> preview audio
//   -> export for drag -> save/restore project -> screenshots of the three screens.
//
//   env OPENCHOIR_AI_DIR=<repo>/ai OPENCHOIR_PYTHON=python3 OPENCHOIR_ENGINE=test
//       OPENCHOIR_RENDERS_DIR=/tmp/renders  openchoir_ai_tests vocal.wav outdir

#include "AIProcessor.h"
#include "AIEditor.h"
#include <iostream>

using namespace openchoir;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cout << "FAIL: " << msg << "\n"; ++fails; } else std::cout << "ok:   " << msg << "\n"; } while (0)

static void snapshot (AIProcessor& p, int page, const juce::File& file)
{
    std::unique_ptr<AIEditor> e (static_cast<AIEditor*> (p.createEditor()));
    e->showPage (page);
    for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.5f);
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

static float peakOf (AIProcessor& p, int blocks, bool& finite, float& peakR)
{
    juce::MidiBuffer midi; float peak = 0; finite = true; peakR = 0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, 512);
        p.processBlock (buf, midi);
        for (int i = 0; i < 512; ++i)
        {
            finite &= std::isfinite (buf.getSample (0, i)) && std::isfinite (buf.getSample (1, i));
            peak = std::max (peak, std::abs (buf.getSample (0, i)));
            peakR = std::max (peakR, std::abs (buf.getSample (1, i)));
        }
    }
    return peak;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::cout << "usage: openchoir_ai_tests vocal.wav outdir\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File vocal (argv[1]), outDir (argv[2]);
    outDir.createDirectory();

    std::cout << "engine state: " << (paths::engineState() == paths::EngineState::ready ? "ready" : "NOT READY") << "\n";
    juce::MemoryBlock saved;
    {
        AIProcessor p;
        p.prepareToPlay (48000.0, 512);
        snapshot (p, 0, outDir.getChildFile ("screen1_empty.png"));

        auto err = p.loadVocal (vocal);
        CHECK (err.isEmpty(), "vocal loads" + (err.isEmpty() ? juce::String() : ": " + err));
        for (int i = 0; i < 200 && ! p.getKeyEstimate().valid; ++i) juce::Thread::sleep (50);
        auto k = p.getKeyEstimate();
        CHECK (k.valid, "key estimated from the vocal: " + k.name());

        bool fin; float pr;
        p.play (false);
        const float vp = peakOf (p, 300, fin, pr);   // the test vocal starts with 1 s of silence
        CHECK (vp > 0.05f && pr > 0.05f && fin, "vocal preview plays on both channels");
        p.stop();
        snapshot (p, 0, outDir.getChildFile ("screen1_vocal.png"));

        p.settings.model = "gospel";
        p.settings.layers = { "oct_up", "oct_down", "third_up" };
        p.settings.quality = "draft";
        p.settings.stereo = true;
        snapshot (p, 1, outDir.getChildFile ("screen2_choir.png"));

        CHECK (p.startRender(), "render starts");
        float lastProg = -1; bool monotonic = true; bool sawMid = false; juce::StringArray seen;
        for (int i = 0; i < 6000; ++i)
        {
            auto st = p.getRunner().getState();
            const float pg = p.getRunner().getProgress();
            if (pg + 1e-4f < lastProg) monotonic = false;
            if (pg > 0.0f && pg < 1.0f) sawMid = true;
            if (seen.size() < 12 && ! juce::approximatelyEqual (pg, lastProg)) seen.add (juce::String (pg, 2));
            lastProg = pg;
            if (i == 20) snapshot (p, 1, outDir.getChildFile ("screen2_rendering.png"));
            if (st != JobRunner::State::running) break;
            juce::Thread::sleep (50);
        }
        CHECK (p.getRunner().getState() == JobRunner::State::done, "render finishes: " + p.getRunner().getStatus() + " " + p.getRunner().getError());
        CHECK (monotonic && sawMid, "progress bar moves forward only (" + seen.joinIntoString (" ") + ")");
        p.pollRender();
        CHECK (p.hasChoir(), "layers load into the plugin");
        auto ids = p.getLayerIds();
        CHECK (ids.joinIntoString (",") == "main,oct_up,oct_down,third_up", "all requested layers present: " + ids.joinIntoString (","));

        p.play (true);
        const float cp = peakOf (p, 200, fin, pr);
        CHECK (fin && cp > 0.01f && cp < 4.0f, "choir preview is audible and sane (peak " + juce::String (cp, 3) + ")");
        p.stop();

        // mute a layer, change post-FX, then export
        p.apvts.getParameter ("mute_oct_up")->setValueNotifyingHost (1.0f);
        p.apvts.getParameter ("reverb")->setValueNotifyingHost (0.4f);
        p.apvts.getParameter ("mic")->setValueNotifyingHost (p.apvts.getParameter ("mic")->convertTo0to1 (2.0f));
        auto all = p.exportForDrag();
        auto one = p.exportForDrag ("main");
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> ra (fm.createReaderFor (all)), r1 (fm.createReaderFor (one));
        CHECK (ra != nullptr && r1 != nullptr, "drag-out files written: " + all.getFileName() + ", " + one.getFileName());
        if (ra && r1)
        {
            const double secs = (double) ra->lengthInSamples / ra->sampleRate;
            CHECK (secs >= p.getVocalSeconds() && secs <= p.getVocalSeconds() + 2.6, "export length = vocal + reverb tail (" + juce::String (secs, 2) + " s)");
            juce::AudioBuffer<float> b (2, (int) ra->lengthInSamples);
            ra->read (&b, 0, (int) ra->lengthInSamples, 0, true, true);
            CHECK (b.getMagnitude (0, b.getNumSamples()) > 0.01f, "exported choir is not silent");
        }
        snapshot (p, 2, outDir.getChildFile ("screen3_mix.png"));
        p.getStateInformation (saved);
        std::cout << "session: " << p.getSessionDir().getFullPathName() << "\n";
    }
    {
        AIProcessor q;
        q.prepareToPlay (44100.0, 256);
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.hasChoir() && q.hasVocal(), "reopening the project restores the vocal and rendered choir");
        CHECK (q.settings.layers.joinIntoString (",") == "oct_up,oct_down,third_up" && q.settings.quality == "draft",
               "reopening restores the choir settings");
        CHECK (q.apvts.getRawParameterValue ("mute_oct_up")->load() > 0.5f, "reopening restores layer mutes");
        bool fin; float pr;
        q.play (true);
        CHECK (peakOf (q, 300, fin, pr) > 0.01f && fin, "restored choir plays at a different sample rate (44.1k)");
    }
    std::cout << (fails ? "SOME CHECKS FAILED\n" : "ALL CHECKS PASSED\n");
    return fails;
}
