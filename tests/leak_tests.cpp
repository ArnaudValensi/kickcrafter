// Memory-leak exercise for LeakSanitizer (epic/MEMORY.md gate).
//
// Runs the REAL engine, KickCrafterProcessor and KickCrafterEditor (same shared-code
// objects the VST3 links) through many create/use/destroy cycles so that any
// accumulating or teardown leak is reported by LeakSanitizer at process exit. The
// executable is built in the diagnostic sanitizer configuration (tools/memory-check.sh);
// it prints exact cycle counts so the evidence states what was exercised.
//
// `--intentional-leak` is the negative control: test-only code drops one heap block on
// purpose, which the leak check MUST report (nonzero exit). It never runs unless asked.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "plugin/Presets.h"
#include "engine/KickEngine.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace kcf;

namespace
{
int checks = 0, failures = 0;
void require (bool ok, const char* what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL: %s\n", what); std::fflush (stdout); }
}

struct Cycles
{
    int engine = 200, processor = 40, instances = 8, instanceRounds = 5, editor = 25, concurrent = 6;
};

// ---------------------------------------------------------------- engine ----
void engineCycles (int n)
{
    std::vector<float> out (8192), hit (48000);
    for (int c = 0; c < n; ++c)
    {
        auto engine = std::make_unique<KickEngine>();
        const double sr = (c % 3 == 0) ? 44100.0 : (c % 3 == 1) ? 48000.0 : 96000.0;
        engine->prepare (sr);
        KickParams p;
        p.startHz = 100.0f + (float) (c % 50) * 10.0f;
        for (int v = 0; v < 24; ++v)               // more notes than voices: steals + declick paths
            engine->noteOn (p, 30 + (v % 12), 0.5f + 0.5f * (float) (v % 2));
        engine->render (out.data(), (int) out.size());
        engine->noteOn (p, 33, 1.0f);
        engine->render (out.data(), 1024);
        engine->allSoundOff();
        engine->render (out.data(), 256);
        require (engine->activeVoiceCount() == 0, "engine voices stop after allSoundOff");
        const int n2 = renderSingleHit (p, sr, 33, 1.0f, hit.data(), (int) hit.size());
        require (n2 > 0, "renderSingleHit produced samples");
    }
    std::printf ("engine cycles: %d (prepare, 24 overlapping notes, steal, panic, renderSingleHit)\n", n);
}

// ------------------------------------------------------------- processor ----
juce::MidiMessage noteOn (int note, int vel) { return juce::MidiMessage::noteOn (1, note, (juce::uint8) vel); }

void runBlocks (KickCrafterProcessor& p, int blocks, int blockSize, bool withMidi)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (withMidi)
        {
            midi.addEvent (noteOn (33 + (b % 7), 100), 0);
            midi.addEvent (noteOn (40, 127), blockSize / 2);
            if (b % 9 == 8) midi.addEvent (juce::MidiMessage::allSoundOff (1), 3);
            if (b % 11 == 10) midi.addEvent (juce::MidiMessage::allNotesOff (1), 5);
        }
        buffer.clear();
        p.processBlock (buffer, midi);
        require (std::isfinite (buffer.getSample (0, blockSize - 1)), "finite output");
    }
}

void exerciseControls (KickCrafterProcessor& p, int seed)
{
    auto& apvts = p.getState();
    for (auto* id : params::allIds)
    {
        auto* param = apvts.getParameter (id);
        param->setValueNotifyingHost ((float) ((seed * 37 + 11) % 100) / 100.0f);
    }
    p.loadFactoryPreset (seed % 8);
    p.toggleAB();
    p.copyCurrentToOtherSlot();
    p.toggleAB();
    juce::MemoryBlock state;
    p.getStateInformation (state);
    p.setStateInformation (state.getData(), (int) state.getSize());
    const char* junk = "<KickCrafterFable version=\"1\"><param id=\"startFreq\" value=\"nonsense\"/>";
    p.setStateInformation (junk, (int) std::strlen (junk));           // rejected without leaking
    p.setStateInformation (state.getData(), (int) state.getSize());
    p.setUIScalePercent (80 + (seed % 5) * 20);
    p.triggerAudition();
}

void processorCycles (int n)
{
    for (int c = 0; c < n; ++c)
    {
        auto p = std::make_unique<KickCrafterProcessor>();
        const double sr = (c % 2 == 0) ? 48000.0 : 96000.0;
        const int block = (c % 3 == 0) ? 64 : (c % 3 == 1) ? 256 : 1024;
        p->setPlayConfigDetails (0, 2, sr, block);
        p->prepareToPlay (sr, block);
        runBlocks (*p, 30, block, true);
        exerciseControls (*p, c);
        runBlocks (*p, 20, block, true);
        p->prepareToPlay (44100.0, block);                        // sample-rate change with live voices
        runBlocks (*p, 10, block, true);
        p->reset();
        p->releaseResources();
        if (c % 2 == 1) { p->prepareToPlay (sr, block); runBlocks (*p, 5, block, false); }   // re-prepare after release
    }
    std::printf ("processor cycles: %d (prepare/process/MIDI incl. CC120/123, every parameter, presets, A/B, state save+restore+junk, 44.1k re-prepare, release)\n", n);
}

void multiInstanceCycles (int instances, int rounds)
{
    for (int r = 0; r < rounds; ++r)
    {
        std::vector<std::unique_ptr<KickCrafterProcessor>> all;
        for (int i = 0; i < instances; ++i)
        {
            all.push_back (std::make_unique<KickCrafterProcessor>());
            all.back()->setPlayConfigDetails (0, 2, 48000.0, 128);
            all.back()->prepareToPlay (48000.0, 128);
        }
        for (int b = 0; b < 20; ++b)
            for (auto& p : all) runBlocks (*p, 1, 128, true);
        juce::MemoryBlock first;
        all.front()->loadFactoryPreset (5);
        all.front()->getStateInformation (first);
        for (auto& p : all) { p->setStateInformation (first.getData(), (int) first.getSize()); exerciseControls (*p, r); }
        // destroy in mixed order, some while voices are still sounding
        for (size_t i = 0; i < all.size(); i += 2) all[i].reset();
        all.clear();
    }
    std::printf ("multi-instance rounds: %d x %d simultaneous instances (interleaved audio, cross-restored state, mixed-order destruction)\n", rounds, instances);
}

// ---------------------------------------------------------------- editor ----
void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

void editorCycles (int n)
{
    require (juce::Desktop::getInstance().getDisplays().displays.size() > 0, "a display is required (run under Xvfb)");
    int reopened = 0, scaled = 0;
    for (int c = 0; c < n; ++c)
    {
        auto p = std::make_unique<KickCrafterProcessor>();
        p->setPlayConfigDetails (0, 2, 48000.0, 256);
        p->prepareToPlay (48000.0, 256);
        p->setUIScalePercent (c % 2 == 0 ? 100 : 125);
        const int opensPerProcessor = 1 + (c % 3);                 // recreate the editor for the same processor
        for (int o = 0; o < opensPerProcessor; ++o)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
            auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
            require (kc != nullptr, "editor type");
            editor->setVisible (true);
            editor->addToDesktop (0);
            pump (60);
            runBlocks (*p, 4, 256, true);                          // note feedback + LED path while open
            kc->pollNow();
            exerciseControls (*p, c + o);                          // host-side changes reflected by polled bindings
            pump (60);
            kc->getTopBar().onScaleSelected (80 + ((c + o) % 5) * 20); ++scaled;   // the scale menu's own callback
            pump (60);
            editor->setSize (1100, 704);                           // constrained resize path
            pump (40);
            if (auto* knob = kc->findKnob (params::sweep))
            {
                knob->getSlider().setValue (90.0, juce::sendNotificationSync);
                knob->getValueLabel().setText ("120 ms", juce::sendNotificationSync);
            }
            pump (40);
            if ((c + o) % 2 == 0)                                  // remove from the desktop before deletion or not
                editor->removeFromDesktop();
            editor.reset();
            pump (40);
            ++reopened;
        }
        if (c % 4 == 3)                                            // processor destroyed while an editor still exists
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
            editor->addToDesktop (0); editor->setVisible (true); pump (40);
            editor.reset();                                        // (JUCE contract: editor first, then processor)
            pump (20);
        }
        p.reset();
        pump (20);
    }
    pump (300);                                                    // drain pending timers/messages
    std::printf ("editor cycles: %d processors, %d editor open/close cycles, %d scale changes (desktop window, polling, host changes, resize, text entry)\n", n, reopened, scaled);
}

void concurrentAudioEditorCycles (int n)
{
    for (int c = 0; c < n; ++c)
    {
        auto p = std::make_unique<KickCrafterProcessor>();
        p->setPlayConfigDetails (0, 2, 48000.0, 128);
        p->prepareToPlay (48000.0, 128);
        std::atomic<bool> stop { false };
        std::thread audio ([&]
        {
            while (! stop.load()) runBlocks (*p, 2, 128, true);
        });
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        editor->addToDesktop (0); editor->setVisible (true);
        for (int i = 0; i < 6; ++i) { exerciseControls (*p, i); pump (30); }
        editor.reset();
        stop = true;
        audio.join();
        p.reset();
        pump (20);
    }
    std::printf ("concurrent audio+editor cycles: %d (audio thread rendering while presets/A-B/state/editor run on the message thread)\n", n);
}

// ---------------------------------------------------------------- presets ----
// The "..." actions path under the leak checker: name prompt and confirmation boxes are
// opened through the same code the menu runs, answered, and also left open while the
// editor is destroyed (nothing may run against the dead bar).
void presetDialogCycles (int n)
{
    require (juce::Desktop::getInstance().getDisplays().displays.size() > 0, "a display is required (run under Xvfb)");
    using TB = kcf::ui::TopBar;
    auto findBox = [] (const juce::String& title) -> juce::AlertWindow*
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* w = dynamic_cast<juce::AlertWindow*> (desktop.getComponent (i)))
                if (w->getName() == title) return w;
        return nullptr;
    };
    int answered = 0, abandoned = 0;
    for (int c = 0; c < n; ++c)
    {
        auto p = std::make_unique<KickCrafterProcessor>();
        p->setPlayConfigDetails (0, 2, 48000.0, 256);
        p->prepareToPlay (48000.0, 256);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p->createEditor());
        auto* kc = dynamic_cast<KickCrafterEditor*> (editor.get());
        require (kc != nullptr, "editor type");
        editor->setVisible (true);
        editor->addToDesktop (0);
        pump (60);
        auto& bar = kc->getTopBar();
        const juce::String name = "Leak " + juce::String (c);
        bar.performPresetAction (TB::actionSaveAs);                 // prompt -> OK (new name)
        pump (60);
        require (bar.getOpenDialog() != nullptr, "name prompt opened");
        bar.getOpenDialog()->getTextEditor ("name")->setText (name, false);
        bar.getOpenDialog()->triggerButtonClick ("OK");
        pump (60);
        require (bar.getLibrary().find (name) != nullptr, "prompt saved the preset");
        bar.performPresetAction (TB::actionSaveAs);                 // prompt -> OK (same name) -> confirm -> Yes
        pump (60);
        require (bar.getOpenDialog() != nullptr, "second prompt opened");
        bar.getOpenDialog()->triggerButtonClick ("OK");
        pump (60);
        auto* box = findBox ("Overwrite preset");
        require (box != nullptr, "overwrite confirmation opened");
        box->triggerButtonClick (c % 2 == 0 ? "Yes" : "No");
        pump (60);
        bar.performPresetAction (TB::actionSaveAs);                 // prompt -> invalid name -> error box -> OK
        pump (60);
        require (bar.getOpenDialog() != nullptr, "third prompt opened");
        bar.getOpenDialog()->getTextEditor ("name")->setText ("bad/name", false);
        bar.getOpenDialog()->triggerButtonClick ("OK");
        pump (60);
        if (auto* err = findBox ("Preset")) { err->triggerButtonClick ("OK"); pump (60); }
        else require (false, "error box opened");
        answered += 3;
        // leave a dialog open and destroy the editor: alternately the prompt and the confirmation
        if (c % 2 == 0) bar.performPresetAction (TB::actionRename);
        else            bar.performPresetAction (TB::actionDelete);
        pump (60);
        require (c % 2 == 0 ? bar.getOpenDialog() != nullptr : findBox ("Delete preset") != nullptr, "dialog left open");
        editor.reset();
        pump (80);
        require (findBox ("Delete preset") == nullptr && findBox ("Rename preset") == nullptr, "dialogs closed with the editor");
        require (juce::File (juce::CharPointer_UTF8 (std::getenv ("KCF_PRESET_DIR"))).getChildFile (presets::fileNameFor (name)).existsAsFile(),
                 "the abandoned Delete never ran");
        ++abandoned;
        p.reset();
        pump (40);
    }
    std::printf ("preset dialog cycles: %d, %d dialogs answered, %d abandoned with the editor\n", n, answered, abandoned);
    require (answered == 3 * n && abandoned == n, "preset dialog cycles");
}

void presetLibraryCycles (int n)
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("kcf-leak-presets-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    int saved = 0, loaded = 0;
    for (int c = 0; c < n; ++c)
    {
        auto lib = std::make_unique<presets::Library> (dir);
        auto p = std::make_unique<KickCrafterProcessor>();
        p->setPlayConfigDetails (0, 2, 48000.0, 128);
        p->prepareToPlay (48000.0, 128);
        juce::String error;
        for (int i = 0; i < 6; ++i)
        {
            p->loadFactoryPreset (i % 8);
            KickParams k = p->getCurrentParams();
            k.startHz = 100.0f + (float) (c * 6 + i);
            require (lib->save ("Cycle " + juce::String (i), k, error), "library save");
            ++saved;
        }
        lib->rescan();
        for (const auto& preset : lib->presets()) { p->loadPreset (preset); ++loaded; }
        require (lib->rename ("Cycle 1", "Renamed " + juce::String (c), error), "library rename");
        presets::Preset back;
        require (presets::fromXml (presets::toXml ("Round trip", p->getCurrentParams()), back, error), "xml round trip");
        require (lib->remove ("Cycle 0", error), "library remove");
        runBlocks (*p, 4, 128, true);
        p->adoptPresetIdentity (*lib->presets().begin());
        juce::MemoryBlock state;
        p->getStateInformation (state);
        p->setStateInformation (state.getData(), (int) state.getSize());
        lib.reset();
        p.reset();
        dir.deleteRecursively();
    }
    std::printf ("preset library cycles: %d (temp directory, %d saves, %d loads, rename/remove/rescan, XML round trips, identity in state)\n", n, saved, loaded);
}

// ------------------------------------------------------ negative control ----
// Test-only: leaks exactly one 4096-byte block so LeakSanitizer must fail the run.

// LeakSanitizer scans stacks and registers conservatively: a stale copy of the leaked pointer in
// a spilled stack slot would make the block look reachable (seen once in the instrumented -O1
// build). The leak is therefore made in a separate non-inlined frame and the stack area below the
// caller is overwritten afterwards, so the only reference is truly gone.
__attribute__ ((noinline)) void scrubStackBelowCaller()
{
    volatile char scratch[65536];
    for (size_t i = 0; i < sizeof (scratch); i += 64) scratch[i] = (char) i;
    __asm__ __volatile__ ("" : : "r" (scratch) : "memory");
}
__attribute__ ((noinline)) void leakOneBlock()
{
    auto* block = new char[4096];
    std::memset (block, 0x5A, 4096);
    std::printf ("NEGATIVE CONTROL: intentionally leaked %d bytes at %p (test-only code path)\n", 4096, (void*) block);
    __asm__ __volatile__ ("" : : "r" (block) : "memory");
    block = nullptr;                                               // dropped on purpose
}
void intentionalLeakForNegativeControl()
{
    leakOneBlock();
    scrubStackBelowCaller();
}
} // namespace

int main (int argc, char** argv)
{
    // Editors read the user preset folder: isolate it from the real ~/.config.
    juce::File isolatedPresetDir;
    if (std::getenv ("KCF_PRESET_DIR") == nullptr)
    {
        isolatedPresetDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("kcf-leak-tests-presets-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        setenv ("KCF_PRESET_DIR", isolatedPresetDir.getFullPathName().toRawUTF8(), 1);
    }
    struct Cleanup { juce::File d; ~Cleanup() { if (d != juce::File()) d.deleteRecursively(); } } cleanup { isolatedPresetDir };
    Cycles cycles;
    bool leak = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--intentional-leak") leak = true;
        else if (a == "--cycles" && i + 1 < argc)
        {
            const int n = std::atoi (argv[++i]);
            cycles.engine = n * 8; cycles.processor = n * 2; cycles.instanceRounds = std::max (1, n / 5);
            cycles.editor = n; cycles.concurrent = std::max (1, n / 4);
        }
    }
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::printf ("kcf_leak_tests: engine/processor/editor lifecycle cycles under LeakSanitizer\n");
        engineCycles (cycles.engine);
        processorCycles (cycles.processor);
        multiInstanceCycles (cycles.instances, cycles.instanceRounds);
        presetLibraryCycles (std::max (1, cycles.processor / 2));
        presetDialogCycles (std::max (1, cycles.editor / 4));
        editorCycles (cycles.editor);
        concurrentAudioEditorCycles (cycles.concurrent);
        if (leak) intentionalLeakForNegativeControl();
        pump (200);
        std::printf ("%d check(s), %d failure(s): %s\n", checks, failures, failures == 0 ? "PASS" : "FAIL");
    }   // orderly JUCE shutdown (message manager, desktop, fonts) before the leak check at exit
    return failures == 0 ? 0 : 1;
}
