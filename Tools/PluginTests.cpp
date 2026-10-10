// Plugin self-tests: state round trip, all presets x sample rates x layouts, click-free
// voice changes, and an editor screenshot.   openchoir_tests screenshot.png
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cout << "FAIL: " << msg << "\n"; ++fails; } else std::cout << "ok:   " << msg << "\n"; } while (0)

int main (int, char** argv) {
  juce::ScopedJuceInitialiser_GUI init;
  {
    // 1. state round trip
    OpenChoirAudioProcessor a; a.setCurrentProgram (2);
    a.apvts.getParameter ("detune")->setValueNotifyingHost (0.33f);
    juce::MemoryBlock mb; a.getStateInformation (mb);
    OpenChoirAudioProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (auto* p : a.getParameters())
      if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        same &= std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) < 1e-6f;
    CHECK (same, "state save/restore keeps every parameter");
    CHECK (b.getCurrentProgram() == 2, "state save/restore keeps the preset index");

    // 2. every preset, stereo and mono-in, several block sizes, 3 sample rates: finite, sane level
    for (double sr : { 44100.0, 48000.0, 96000.0 })
      for (bool mono : { false, true })
        for (int prog = 0; prog < a.getNumPrograms(); ++prog) {
          OpenChoirAudioProcessor p;
          auto layout = p.getBusesLayout();
          layout.inputBuses.getReference (0) = mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
          bool okLayout = p.setBusesLayout (layout);
          p.setCurrentProgram (prog);
          p.prepareToPlay (sr, 512);
          juce::MidiBuffer midi; double peak = 0; bool finite = true; int pos = 0;
          for (int blk = 0; blk < 400; ++blk) {
            int n = (blk % 3 == 0) ? 512 : (blk % 3 == 1 ? 37 : 256);   // irregular block sizes
            juce::AudioBuffer<float> buf (2, n);
            for (int i = 0; i < n; ++i, ++pos) {
              float s = 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * pos / sr);
              buf.setSample (0, i, s); buf.setSample (1, i, mono ? 0.0f : s);
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) {
              float v = buf.getSample (c, i); finite &= std::isfinite (v); peak = std::max (peak, (double) std::abs (v)); }
          }
          if (! finite || peak > 4.0 || peak < 0.01 || ! okLayout)
            { std::cout << "FAIL: preset " << prog << " sr " << sr << " mono " << mono << " peak " << peak << " finite " << finite << "\n"; ++fails; }
        }
    std::cout << "ok:   all presets x {44.1,48,96} kHz x {stereo, mono in} x odd block sizes checked\n";

    // 3. voice-count change mid-stream must not click (max sample-to-sample jump)
    OpenChoirAudioProcessor p; p.setCurrentProgram (0); p.prepareToPlay (48000, 256);
    auto* vp = p.apvts.getParameter ("voices"); juce::MidiBuffer midi;
    float prev = 0, maxJumpSteady = 0, maxJumpChange = 0; int pos = 0;
    for (int blk = 0; blk < 600; ++blk) {
      if (blk == 300) vp->setValueNotifyingHost (vp->convertTo0to1 (3.0f));
      juce::AudioBuffer<float> buf (2, 256);
      for (int i = 0; i < 256; ++i, ++pos) { float s = 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * pos / 48000.0); buf.setSample (0, i, s); buf.setSample (1, i, s); }
      p.processBlock (buf, midi);
      for (int i = 0; i < 256; ++i) { float v = buf.getSample (0, i); float j = std::abs (v - prev); prev = v;
        if (blk > 100 && blk < 300) maxJumpSteady = std::max (maxJumpSteady, j);
        if (blk >= 300 && blk < 320) maxJumpChange = std::max (maxJumpChange, j); }
    }
    CHECK (maxJumpChange < maxJumpSteady * 1.5f, "changing Voices 12 -> 3 mid-note is click-free (jump " + std::to_string (maxJumpChange) + " vs steady " + std::to_string (maxJumpSteady) + ")");
  }
  // 4. editor snapshot
  OpenChoirAudioProcessor p; p.setCurrentProgram (0);
  std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
  auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
  juce::File f (argv[1]); f.deleteFile();
  juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
  std::cout << (fails ? "SOME CHECKS FAILED\n" : "ALL CHECKS PASSED\n");
  return fails;
}
