// Minimal real VST3 host for the memory-leak gate (docs/development.md).
//
// Loads the ACTUAL built "KickCrafter.vst3" module with dlopen and drives it exactly
// the way a host does, through the public VST3 interfaces only (no JUCE, no test hooks):
//   ModuleEntry -> GetPluginFactory -> IPluginFactory3::setHostContext (with a Linux IRunLoop)
//   -> IComponent + IEditController (connected) -> buses, setupProcessing, setActive,
//   setProcessing -> process() with note events, parameter automation and a MIDI CC mapping
//   -> getState/setState/setComponentState -> IPlugView attached to a real X11 window under
//   the host run loop (timers + file descriptors), resized, removed -> teardown ->
//   ModuleExit -> leak check -> dlclose.
// Several instances are also alive simultaneously. The run repeats for N cycles and prints
// exact counts. Run under LeakSanitizer (tools/memory-check.sh); `--intentional-leak` is the
// host-side negative control (a dropped block the leak check must report).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <pluginterfaces/base/funknown.cpp>
#include <pluginterfaces/base/coreiids.cpp>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>
#include <public.sdk/source/vst/vstinitiids.cpp>

#include <X11/Xlib.h>
#include <dlfcn.h>
#include <poll.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Steinberg
{
    DEF_CLASS_IID (IPlugView)
    DEF_CLASS_IID (IPlugFrame)
    DEF_CLASS_IID (IPlugViewContentScaleSupport)
    DEF_CLASS_IID (Linux::IRunLoop)
    DEF_CLASS_IID (Linux::IEventHandler)
    DEF_CLASS_IID (Linux::ITimerHandler)
}

extern "C" void __lsan_do_leak_check() __attribute__ ((weak));

using namespace Steinberg;

namespace
{
int checks = 0, failures = 0;
void require (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL: %s\n", what.c_str()); std::fflush (stdout); }
}

// Host-owned COM objects: reference counts are tracked (and reported) but the host owns the
// lifetime, so an over-release by the plug-in cannot free them.
template <typename First, typename... Rest>
struct HostObject : public First, public Rest...
{
    std::atomic<uint32> refs { 1 };
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    template <typename I> bool match (const TUID iid, void** obj)
    {
        if (! FUnknownPrivate::iidEqual (iid, I::iid)) return false;
        *obj = static_cast<I*> (this); addRef(); return true;
    }
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (match<First> (iid, obj) || (match<Rest> (iid, obj) || ...)) return kResultOk;
        if (FUnknownPrivate::iidEqual (iid, FUnknown::iid))
        {
            *obj = static_cast<FUnknown*> (static_cast<First*> (this)); addRef(); return kResultOk;
        }
        *obj = nullptr; return kNoInterface;
    }
    virtual ~HostObject() = default;
};

// ----------------------------------------------------------- run loop ----
struct RunLoop final : HostObject<Linux::IRunLoop>
{
    struct FdReg { Linux::IEventHandler* handler; int fd; };
    struct TimerReg { Linux::ITimerHandler* handler; uint64 ms; std::chrono::steady_clock::time_point next; };
    std::vector<FdReg> fds;
    std::vector<TimerReg> timers;
    int fdRegistrations = 0, timerRegistrations = 0, fdCallbacks = 0, timerCallbacks = 0;
    Display* display = nullptr;

    tresult PLUGIN_API registerEventHandler (Linux::IEventHandler* handler, Linux::FileDescriptor fd) override
    {
        if (handler == nullptr) return kInvalidArgument;
        handler->addRef(); fds.push_back ({ handler, fd }); ++fdRegistrations; return kResultTrue;
    }
    tresult PLUGIN_API unregisterEventHandler (Linux::IEventHandler* handler) override
    {
        bool found = false;
        for (auto it = fds.begin(); it != fds.end();)
            if (it->handler == handler) { it->handler->release(); it = fds.erase (it); found = true; } else ++it;
        return found ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API registerTimer (Linux::ITimerHandler* handler, Linux::TimerInterval ms) override
    {
        if (handler == nullptr) return kInvalidArgument;
        handler->addRef();
        timers.push_back ({ handler, ms, std::chrono::steady_clock::now() + std::chrono::milliseconds (ms) });
        ++timerRegistrations; return kResultTrue;
    }
    tresult PLUGIN_API unregisterTimer (Linux::ITimerHandler* handler) override
    {
        bool found = false;
        for (auto it = timers.begin(); it != timers.end();)
            if (it->handler == handler) { it->handler->release(); it = timers.erase (it); found = true; } else ++it;
        return found ? kResultTrue : kResultFalse;
    }

    // Dispatch registered descriptors and timers (plus the host's own X connection) for `ms`.
    void runFor (int ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (ms);
        while (true)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count();
            for (auto& t : timers)
                wait = std::min<long> (wait, std::max<long> (0, std::chrono::duration_cast<std::chrono::milliseconds> (t.next - now).count()));
            std::vector<pollfd> pfds;
            for (auto& f : fds) pfds.push_back ({ f.fd, POLLIN, 0 });
            const int xfd = display != nullptr ? ConnectionNumber (display) : -1;
            if (xfd >= 0) pfds.push_back ({ xfd, POLLIN, 0 });
            const int ready = ::poll (pfds.data(), (nfds_t) pfds.size(), (int) std::min<long> (wait, 20));
            if (ready > 0)
            {
                const auto snapshot = fds;                    // handlers may (un)register while called
                for (size_t i = 0; i < snapshot.size(); ++i)
                    if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
                    {
                        const bool stillRegistered = std::any_of (fds.begin(), fds.end(), [&] (const FdReg& r) { return r.handler == snapshot[i].handler && r.fd == snapshot[i].fd; });
                        if (stillRegistered) { ++fdCallbacks; snapshot[i].handler->onFDIsSet (snapshot[i].fd); }
                    }
            }
            if (display != nullptr)
                while (XPending (display) > 0) { XEvent e; XNextEvent (display, &e); }
            const auto after = std::chrono::steady_clock::now();
            const auto due = timers;                          // copy: onTimer may unregister
            for (auto& t : due)
                if (after >= t.next)
                {
                    for (auto& live : timers)
                        if (live.handler == t.handler) live.next = after + std::chrono::milliseconds (live.ms);
                    if (std::any_of (timers.begin(), timers.end(), [&] (const TimerReg& r) { return r.handler == t.handler; }))
                    { ++timerCallbacks; t.handler->onTimer(); }
                }
        }
    }
};

// ------------------------------------------------------- host context ----
struct HostApplication final : HostObject<Vst::IHostApplication>
{
    RunLoop& runLoop;
    explicit HostApplication (RunLoop& r) : runLoop (r) {}
    tresult PLUGIN_API getName (Vst::String128 name) override
    {
        const char16_t* n = u"KickCrafter leak host";
        size_t i = 0; for (; n[i] != 0 && i < 127; ++i) name[i] = (Vst::TChar) n[i]; name[i] = 0; return kResultTrue;
    }
    tresult PLUGIN_API createInstance (TUID, TUID, void** obj) override { *obj = nullptr; return kNotImplemented; }
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (iid, Linux::IRunLoop::iid)) { *obj = static_cast<Linux::IRunLoop*> (&runLoop); runLoop.addRef(); return kResultOk; }
        return HostObject::queryInterface (iid, obj);
    }
};

struct PlugFrame final : HostObject<IPlugFrame>
{
    RunLoop& runLoop; Display* display = nullptr; Window window = 0; int resizeRequests = 0;
    int budget = 1000000, refused = 0;   // resizeView calls left before the frame refuses (a runaway host/plug-in ping-pong)
    explicit PlugFrame (RunLoop& r) : runLoop (r) {}
    tresult PLUGIN_API resizeView (IPlugView* view, ViewRect* newSize) override
    {
        ++resizeRequests;
        if (budget <= 0) { ++refused; return kResultFalse; }
        --budget;
        if (display != nullptr && window != 0 && newSize != nullptr)
        {
            XResizeWindow (display, window, (unsigned) std::max (1, newSize->getWidth()), (unsigned) std::max (1, newSize->getHeight()));
            XSync (display, False);
        }
        return view != nullptr ? view->onSize (newSize) : kResultFalse;
    }
    tresult PLUGIN_API queryInterface (const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual (iid, Linux::IRunLoop::iid)) { *obj = static_cast<Linux::IRunLoop*> (&runLoop); runLoop.addRef(); return kResultOk; }
        return HostObject::queryInterface (iid, obj);
    }
};

struct ComponentHandler final : HostObject<Vst::IComponentHandler>
{
    int begins = 0, performs = 0, ends = 0, restarts = 0;
    tresult PLUGIN_API beginEdit (Vst::ParamID) override { ++begins; return kResultOk; }
    tresult PLUGIN_API performEdit (Vst::ParamID, Vst::ParamValue) override { ++performs; return kResultOk; }
    tresult PLUGIN_API endEdit (Vst::ParamID) override { ++ends; return kResultOk; }
    tresult PLUGIN_API restartComponent (int32) override { ++restarts; return kResultOk; }
};

struct MemoryStream final : HostObject<IBStream>
{
    std::vector<char> data; int64 pos = 0;
    tresult PLUGIN_API read (void* buffer, int32 n, int32* numRead) override
    {
        const int64 avail = std::max<int64> (0, (int64) data.size() - pos);
        const int32 count = (int32) std::min<int64> (n, avail);
        if (count > 0) std::memcpy (buffer, data.data() + pos, (size_t) count);
        pos += count; if (numRead) *numRead = count; return kResultTrue;
    }
    tresult PLUGIN_API write (void* buffer, int32 n, int32* numWritten) override
    {
        if (pos + n > (int64) data.size()) data.resize ((size_t) (pos + n));
        std::memcpy (data.data() + pos, buffer, (size_t) n); pos += n; if (numWritten) *numWritten = n; return kResultTrue;
    }
    tresult PLUGIN_API seek (int64 p, int32 mode, int64* result) override
    {
        if (mode == kIBSeekSet) pos = p; else if (mode == kIBSeekCur) pos += p; else pos = (int64) data.size() + p;
        pos = std::max<int64> (0, pos); if (result) *result = pos; return kResultTrue;
    }
    tresult PLUGIN_API tell (int64* p) override { if (p) *p = pos; return kResultTrue; }
    void rewind() { pos = 0; }
};

struct EventList final : HostObject<Vst::IEventList>
{
    std::vector<Vst::Event> events;
    int32 PLUGIN_API getEventCount() override { return (int32) events.size(); }
    tresult PLUGIN_API getEvent (int32 i, Vst::Event& e) override { if (i < 0 || i >= (int32) events.size()) return kResultFalse; e = events[(size_t) i]; return kResultTrue; }
    tresult PLUGIN_API addEvent (Vst::Event& e) override { events.push_back (e); return kResultTrue; }
    void noteOn (int32 offset, int16 pitch, float velocity)
    {
        Vst::Event e {}; e.busIndex = 0; e.sampleOffset = offset; e.type = Vst::Event::kNoteOnEvent;
        e.noteOn.channel = 0; e.noteOn.pitch = pitch; e.noteOn.velocity = velocity; e.noteOn.noteId = -1; events.push_back (e);
    }
};

struct ParamQueue final : HostObject<Vst::IParamValueQueue>
{
    Vst::ParamID id = 0; std::vector<std::pair<int32, Vst::ParamValue>> points;
    Vst::ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return (int32) points.size(); }
    tresult PLUGIN_API getPoint (int32 i, int32& offset, Vst::ParamValue& value) override
    {
        if (i < 0 || i >= (int32) points.size()) return kResultFalse; offset = points[(size_t) i].first; value = points[(size_t) i].second; return kResultTrue;
    }
    tresult PLUGIN_API addPoint (int32 offset, Vst::ParamValue value, int32& index) override { points.emplace_back (offset, value); index = (int32) points.size() - 1; return kResultTrue; }
};

struct ParameterChanges final : HostObject<Vst::IParameterChanges>
{
    std::vector<std::unique_ptr<ParamQueue>> queues;
    int32 PLUGIN_API getParameterCount() override { return (int32) queues.size(); }
    Vst::IParamValueQueue* PLUGIN_API getParameterData (int32 i) override { return (i >= 0 && i < (int32) queues.size()) ? queues[(size_t) i].get() : nullptr; }
    Vst::IParamValueQueue* PLUGIN_API addParameterData (const Vst::ParamID& id, int32& index) override
    {
        for (size_t i = 0; i < queues.size(); ++i) if (queues[i]->id == id) { index = (int32) i; return queues[i].get(); }
        queues.push_back (std::make_unique<ParamQueue>()); queues.back()->id = id; index = (int32) queues.size() - 1; return queues.back().get();
    }
    void clear() { queues.clear(); }
};

bool titleEquals (const Vst::String128 title, const char* ascii)
{
    for (int i = 0; i < 128; ++i)
    {
        if (ascii[i] == 0) return title[i] == 0;
        if ((Vst::TChar) ascii[i] != title[i]) return false;
    }
    return false;
}

// ----------------------------------------------------------- instance ----
struct Instance
{
    Vst::IComponent* component = nullptr;
    Vst::IAudioProcessor* processor = nullptr;
    Vst::IEditController* controller = nullptr;
    Vst::IConnectionPoint* componentCP = nullptr;
    Vst::IConnectionPoint* controllerCP = nullptr;
    Vst::ParamID driveId = 0, allSoundOffId = 0;
    bool haveDrive = false, haveCC = false;
    std::vector<float> left, right;
    int64 position = 0;
    double sampleRate = 48000.0;
    int blockSize = 512;
    int blocksProcessed = 0, eventsSent = 0;
    double energy = 0.0;

    bool create (IPluginFactory* factory, const TUID cid, HostApplication& host, ComponentHandler& handler, double sr, int block)
    {
        sampleRate = sr; blockSize = block;
        require (factory->createInstance (cid, Vst::IComponent::iid, (void**) &component) == kResultOk && component != nullptr, "createInstance IComponent");
        if (component == nullptr) return false;
        require (component->initialize (static_cast<Vst::IHostApplication*> (&host)) == kResultOk, "component initialize");
        TUID controllerCid {};
        require (component->getControllerClassId (controllerCid) == kResultOk, "getControllerClassId");
        require (factory->createInstance (controllerCid, Vst::IEditController::iid, (void**) &controller) == kResultOk && controller != nullptr, "createInstance IEditController");
        if (controller == nullptr) return false;
        require (controller->initialize (static_cast<Vst::IHostApplication*> (&host)) == kResultOk, "controller initialize");
        require (controller->setComponentHandler (&handler) == kResultOk, "setComponentHandler");
        component->queryInterface (Vst::IConnectionPoint::iid, (void**) &componentCP);
        controller->queryInterface (Vst::IConnectionPoint::iid, (void**) &controllerCP);
        require (componentCP != nullptr && controllerCP != nullptr, "connection points");
        if (componentCP && controllerCP) { componentCP->connect (controllerCP); controllerCP->connect (componentCP); }
        require (component->queryInterface (Vst::IAudioProcessor::iid, (void**) &processor) == kResultOk && processor != nullptr, "IAudioProcessor");
        if (processor == nullptr) return false;
        require (component->getBusCount (Vst::kAudio, Vst::kOutput) == 1, "one audio output bus");
        require (component->getBusCount (Vst::kEvent, Vst::kInput) == 1, "one event input bus");
        Vst::SpeakerArrangement stereo = Vst::SpeakerArr::kStereo;
        require (processor->setBusArrangements (nullptr, 0, &stereo, 1) == kResultOk, "setBusArrangements stereo");
        require (component->activateBus (Vst::kAudio, Vst::kOutput, 0, true) == kResultOk, "activate audio out");
        require (component->activateBus (Vst::kEvent, Vst::kInput, 0, true) == kResultOk, "activate event in");
        Vst::ProcessSetup setup { Vst::kRealtime, Vst::kSample32, blockSize, sampleRate };
        require (processor->setupProcessing (setup) == kResultOk, "setupProcessing");
        require (component->setActive (true) == kResultOk, "setActive true");
        require (processor->setProcessing (true) == kResultOk, "setProcessing true");
        left.assign ((size_t) blockSize, 0.0f); right.assign ((size_t) blockSize, 0.0f);
        // Parameter ids by title (JUCE hashes string ids into VST3 ParamIDs).
        const int32 count = controller->getParameterCount();
        require (count == 13, "exactly the 12 controls + Bypass are exported (no synthetic MIDI CC parameters, v1.1)");
        for (int32 i = 0; i < count; ++i)
        {
            Vst::ParameterInfo info {};
            if (controller->getParameterInfo (i, info) != kResultOk) continue;
            if (titleEquals (info.title, "Drive")) { driveId = info.id; haveDrive = true; }
        }
        require (haveDrive, "Drive parameter found by title");
        Vst::IMidiMapping* mapping = nullptr;
        if (controller->queryInterface (Vst::IMidiMapping::iid, (void**) &mapping) == kResultOk && mapping != nullptr)
        {
            haveCC = mapping->getMidiControllerAssignment (0, 0, Vst::kCtrlAllSoundsOff, allSoundOffId) == kResultTrue;
            mapping->release();
        }
        require (! haveCC, "no IMidiMapping assignment for CC120: MIDI CC emulation is off (v1.1), so CC events are not delivered through VST3");
        return true;
    }

    void process (int blocks, bool playing, bool withNotes, bool automateDrive, bool sendAllSoundOff)
    {
        for (int b = 0; b < blocks; ++b)
        {
            EventList events; ParameterChanges changes;
            if (withNotes)
            {
                events.noteOn (0, (int16) (33 + (b % 7)), 0.9f);
                events.noteOn (blockSize / 3, 40, 1.0f);
                eventsSent += 2;
            }
            int32 idx = 0;
            if (automateDrive && haveDrive && b % 4 == 1)
                changes.addParameterData (driveId, idx)->addPoint (0, (b % 8) / 8.0, idx);
            if (sendAllSoundOff && haveCC && b == blocks - 2)
                changes.addParameterData (allSoundOffId, idx)->addPoint (7, 1.0, idx);
            std::fill (left.begin(), left.end(), 0.0f); std::fill (right.begin(), right.end(), 0.0f);
            float* chans[2] = { left.data(), right.data() };
            Vst::AudioBusBuffers out; out.numChannels = 2; out.silenceFlags = 0; out.channelBuffers32 = chans;
            Vst::ProcessContext ctx {};
            ctx.state = (playing ? Vst::ProcessContext::kPlaying : 0) | Vst::ProcessContext::kTempoValid | Vst::ProcessContext::kProjectTimeMusicValid;
            ctx.sampleRate = sampleRate; ctx.projectTimeSamples = position; ctx.tempo = 120.0; ctx.projectTimeMusic = (double) position / sampleRate * 2.0;
            Vst::ProcessData data;
            data.processMode = Vst::kRealtime; data.symbolicSampleSize = Vst::kSample32; data.numSamples = blockSize;
            data.numInputs = 0; data.inputs = nullptr; data.numOutputs = 1; data.outputs = &out;
            data.inputParameterChanges = &changes; data.inputEvents = &events; data.processContext = &ctx;
            require (processor->process (data) == kResultOk, "process");
            for (int i = 0; i < blockSize; ++i) { require (std::isfinite (left[(size_t) i]), "finite output"); energy += std::fabs (left[(size_t) i]); }
            position += blockSize; ++blocksProcessed;
        }
    }

    void stateRoundTrip()
    {
        MemoryStream stream;
        require (component->getState (&stream) == kResultOk, "getState");
        require (stream.data.size() > 32, "state has content");
        stream.rewind();
        require (controller->setComponentState (&stream) == kResultOk, "setComponentState");
        stream.rewind();
        require (component->setState (&stream) == kResultOk, "setState");
        MemoryStream controllerState;
        controller->getState (&controllerState);             // may be empty for this plug-in
        if (haveDrive)
        {
            require (controller->setParamNormalized (driveId, 0.7) == kResultOk, "controller setParamNormalized");
            require (std::fabs (controller->getParamNormalized (driveId) - 0.7) < 1e-6, "controller getParamNormalized");
        }
        require (stream.refs == 1 && controllerState.refs == 1, "stream references balanced");
    }

    void destroy()
    {
        if (processor) processor->setProcessing (false);
        if (component) component->setActive (false);
        if (componentCP && controllerCP) { componentCP->disconnect (controllerCP); controllerCP->disconnect (componentCP); }
        if (componentCP) { componentCP->release(); componentCP = nullptr; }
        if (controllerCP) { controllerCP->release(); controllerCP = nullptr; }
        if (controller) { controller->terminate(); }
        if (component) { component->terminate(); }
        if (processor) { processor->release(); processor = nullptr; }
        if (controller) { controller->release(); controller = nullptr; }
        if (component) { component->release(); component = nullptr; }
    }
};

struct ViewSession
{
    int attached = 0, resized = 0, hostResizeRequests = 0, scaleFactorsApplied = 0, negotiations = 0;

    // The sequence a Steinberg host plays on a Windows screen scaled above 100 % (Cubase 10 and
    // later): the view gets the display's content scale factor, then the host negotiates every
    // size through checkSizeConstraint before onSize, including sizes the editor's constrainer
    // must correct (off aspect, out of range). Each step tolerates a handful of resizeView
    // callbacks from the plug-in; a ping-pong past the budget is refused by the frame and
    // counted as a failure, a recursion would crash this process. A tester's Cubase 11 on
    // Windows 10 crashed at the opening of the editor (2026-09-26); this reproduces the
    // host side of that opening with the Linux build.
    void cubaseNegotiation (IPlugView* view, RunLoop& runLoop, PlugFrame& frame, Display* display, Window window, int millis)
    {
        IPlugViewContentScaleSupport* scaling = nullptr;
        require (view->queryInterface (IPlugViewContentScaleSupport::iid, (void**) &scaling) == kResultOk && scaling != nullptr,
                 "view supports IPlugViewContentScaleSupport");
        if (scaling == nullptr) return;
        const double factors[] = { 1.25, 1.5, 2.0, 1.0 };
        for (double factor : factors)
        {
            const int before = frame.resizeRequests;
            frame.budget = 16; frame.refused = 0;
            require (scaling->setContentScaleFactor ((IPlugViewContentScaleSupport::ScaleFactor) factor) == kResultTrue,
                     "setContentScaleFactor " + std::to_string (factor));
            ++scaleFactorsApplied;
            runLoop.runFor (millis / 4);
            ViewRect size;
            require (view->getSize (&size) == kResultOk && size.getWidth() > 0 && size.getHeight() > 0, "getSize after scale");
            // 1. The host applies the size the plug-in reports (what Cubase does right after the scale).
            XResizeWindow (display, window, (unsigned) std::max (1, size.getWidth()), (unsigned) std::max (1, size.getHeight())); XSync (display, False);
            require (view->onSize (&size) == kResultOk, "onSize with the reported size");
            runLoop.runFor (millis / 8);
            // 2. Proposals the constrainer must correct: off aspect, below and above the limits.
            const ViewRect proposals[] = { ViewRect (0, 0, (int32) (size.getWidth() * 1.3), (int32) (size.getHeight() * 1.05)),
                                           ViewRect (0, 0, 300, 900), ViewRect (0, 0, 4000, 300), ViewRect (0, 0, 1000, 640) };
            for (ViewRect proposal : proposals)
            {
                ViewRect corrected = proposal;
                view->checkSizeConstraint (&corrected);
                require (corrected.getWidth() > 0 && corrected.getHeight() > 0, "checkSizeConstraint returns a usable size");
                XResizeWindow (display, window, (unsigned) std::max (1, corrected.getWidth()), (unsigned) std::max (1, corrected.getHeight())); XSync (display, False);
                require (view->onSize (&corrected) == kResultOk, "onSize with the corrected size");
                runLoop.runFor (millis / 8);
                // A second checkSizeConstraint on what we just applied must be a fixed point.
                ViewRect again = corrected;
                view->checkSizeConstraint (&again);
                require (std::abs (again.getWidth() - corrected.getWidth()) <= 1 && std::abs (again.getHeight() - corrected.getHeight()) <= 1,
                         "the corrected size is a fixed point of checkSizeConstraint");
                ++negotiations;
            }
            require (frame.refused == 0, "no runaway resizeView ping-pong at scale " + std::to_string (factor)
                                             + " (" + std::to_string (frame.resizeRequests - before) + " host requests)");
            frame.budget = 1000000;
        }
        scaling->release();
    }
    // Create the editor view, attach it to a fresh X11 window, run the host loop, resize, remove.
    void open (Instance& inst, RunLoop& runLoop, PlugFrame& frame, Display* display, int millis, bool processWhileOpen)
    {
        IPlugView* view = inst.controller->createView (Vst::ViewType::kEditor);
        require (view != nullptr, "createView editor");
        if (view == nullptr) return;
        require (view->isPlatformTypeSupported (kPlatformTypeX11EmbedWindowID) == kResultTrue, "X11 embed supported");
        require (view->setFrame (&frame) == kResultOk, "setFrame");
        ViewRect size;
        require (view->getSize (&size) == kResultOk && size.getWidth() > 0 && size.getHeight() > 0, "getSize");
        const Window root = DefaultRootWindow (display);
        const Window window = XCreateSimpleWindow (display, root, 40, 40, (unsigned) std::max (1, size.getWidth()), (unsigned) std::max (1, size.getHeight()), 0, 0, 0x202020);
        XSelectInput (display, window, StructureNotifyMask | ExposureMask);
        XStoreName (display, window, "KickCrafter leak host");
        XMapWindow (display, window); XSync (display, False);
        frame.display = display; frame.window = window;
        require (view->attached ((void*) window, kPlatformTypeX11EmbedWindowID) == kResultOk, "attached");
        ++attached;
        runLoop.runFor (millis);
        if (processWhileOpen) { inst.process (6, true, true, false, false); runLoop.runFor (millis / 2); }
        if (view->canResize() == kResultTrue)
        {
            ViewRect bigger (0, 0, 1250, 800);
            view->checkSizeConstraint (&bigger);
            XResizeWindow (display, window, (unsigned) bigger.getWidth(), (unsigned) bigger.getHeight()); XSync (display, False);
            require (view->onSize (&bigger) == kResultOk, "onSize");
            ++resized;
            runLoop.runFor (millis / 2);
            ViewRect back (0, 0, 1000, 640);
            view->checkSizeConstraint (&back);
            XResizeWindow (display, window, (unsigned) back.getWidth(), (unsigned) back.getHeight()); XSync (display, False);
            view->onSize (&back); ++resized;
            runLoop.runFor (millis / 2);
            cubaseNegotiation (view, runLoop, frame, display, window, millis);
        }
        require (view->removed() == kResultOk, "removed");
        frame.display = nullptr; frame.window = 0;
        hostResizeRequests = frame.resizeRequests;
        const uint32 remaining = view->release();
        require (remaining == 0, "view released by the host (refcount 0)");
        XDestroyWindow (display, window); XSync (display, False);
        runLoop.runFor (millis / 4);
    }
};


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
    auto* block = new char[8192];
    std::memset (block, 0x3C, 8192);
    std::printf ("NEGATIVE CONTROL: host intentionally leaked %d bytes at %p\n", 8192, (void*) block);
    __asm__ __volatile__ ("" : : "r" (block) : "memory");
    block = nullptr;
}
void intentionalLeakForNegativeControl()
{
    leakOneBlock();
    scrubStackBelowCaller();
}
} // namespace

int main (int argc, char** argv)
{
    std::string modulePath;
    int cycles = 5, viewMillis = 350, simultaneous = 3;
    bool wantView = true, leak = false, doDlclose = true;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--cycles" && i + 1 < argc) cycles = std::atoi (argv[++i]);
        else if (a == "--view-ms" && i + 1 < argc) viewMillis = std::atoi (argv[++i]);
        else if (a == "--no-view") wantView = false;
        else if (a == "--no-dlclose") doDlclose = false;
        else if (a == "--intentional-leak") leak = true;
        else modulePath = a;
    }
    if (modulePath.empty()) { std::fprintf (stderr, "usage: kcf_vst3_host [--cycles N] [--view-ms MS] [--no-view] [--no-dlclose] [--intentional-leak] <module.so>\n"); return 2; }

    Display* display = wantView ? XOpenDisplay (nullptr) : nullptr;
    require (! wantView || display != nullptr, "X display available for the view (DISPLAY)");
    if (wantView && display == nullptr) return 1;

    void* lib = dlopen (modulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) { std::printf ("  FAIL: dlopen %s: %s\n", modulePath.c_str(), dlerror()); return 1; }
    auto moduleEntry = (bool (*) (void*)) dlsym (lib, "ModuleEntry");
    auto moduleExit = (bool (*)()) dlsym (lib, "ModuleExit");
    auto getFactory = (IPluginFactory* (*)()) dlsym (lib, "GetPluginFactory");
    require (moduleEntry && moduleExit && getFactory, "ModuleEntry/ModuleExit/GetPluginFactory exported");
    if (! (moduleEntry && moduleExit && getFactory)) return 1;
    require (moduleEntry (lib), "ModuleEntry");

    RunLoop runLoop; runLoop.display = display;
    HostApplication host (runLoop);
    PlugFrame frame (runLoop);
    ComponentHandler handler;

    IPluginFactory* factory = getFactory();
    require (factory != nullptr, "GetPluginFactory");
    if (factory == nullptr) return 1;
    IPluginFactory3* factory3 = nullptr;
    if (factory->queryInterface (IPluginFactory3::iid, (void**) &factory3) == kResultOk && factory3 != nullptr)
    {
        require (factory3->setHostContext (static_cast<Vst::IHostApplication*> (&host)) == kResultOk, "setHostContext (host run loop offered)");
        factory3->release();
    }
    TUID effectCid {}; bool found = false; std::string className;
    for (int32 i = 0; i < factory->countClasses(); ++i)
    {
        PClassInfo info {};
        if (factory->getClassInfo (i, &info) != kResultOk) continue;
        if (std::strcmp (info.category, kVstAudioEffectClass) == 0) { std::memcpy (effectCid, info.cid, sizeof (TUID)); className = info.name; found = true; break; }
    }
    require (found, "audio module class found in the factory");
    if (! found) return 1;
    std::printf ("kcf_vst3_host: module %s, class '%s', %d cycle(s), view %s\n", modulePath.c_str(), className.c_str(), cycles, wantView ? "attached to X11" : "off");

    ViewSession views;
    int instancesCreated = 0, blocks = 0, events = 0, stateRoundTrips = 0;
    for (int c = 0; c < cycles; ++c)
    {
        const double sr = (c % 3 == 0) ? 44100.0 : (c % 3 == 1) ? 48000.0 : 96000.0;
        Instance inst;
        if (! inst.create (factory, effectCid, host, handler, sr, (c % 2 == 0) ? 512 : 256)) { inst.destroy(); break; }
        ++instancesCreated;
        inst.process (24, true, true, true, false);           // notes + Drive automation while notes sound
        inst.process (12, false, true, false, true);          // stopped transport, CC120 near the end
        inst.stateRoundTrip(); ++stateRoundTrips;
        inst.process (8, true, true, false, false);
        if (wantView)
        {
            views.open (inst, runLoop, frame, display, viewMillis, true);
            if (c % 2 == 0) views.open (inst, runLoop, frame, display, viewMillis / 2, false);     // reopen on the same instance
        }
        inst.process (8, true, false, false, false);
        require (inst.energy > 0.0, "audio was produced");
        blocks += inst.blocksProcessed; events += inst.eventsSent;
        inst.destroy();
        runLoop.runFor (30);
    }
    // Several instances alive at once (their own controllers/views), interleaved processing.
    {
        std::vector<std::unique_ptr<Instance>> all;
        for (int i = 0; i < simultaneous; ++i)
        {
            all.push_back (std::make_unique<Instance>());
            if (! all.back()->create (factory, effectCid, host, handler, 48000.0, 256)) break;
            ++instancesCreated;
        }
        for (int b = 0; b < 16; ++b)
            for (auto& inst : all) inst->process (1, true, true, b % 3 == 0, false);
        for (auto& inst : all) { inst->stateRoundTrip(); ++stateRoundTrips; }
        if (wantView && ! all.empty()) views.open (*all.front(), runLoop, frame, display, viewMillis / 2, true);
        for (auto& inst : all) { blocks += inst->blocksProcessed; events += inst->eventsSent; }
        for (size_t i = 0; i < all.size(); i += 2) { all[i]->destroy(); all[i].reset(); }
        for (auto& inst : all) if (inst) inst->destroy();
        all.clear();
        runLoop.runFor (30);
    }
    if (leak) intentionalLeakForNegativeControl();               // negative control, before the drain
    factory->release();
    require (moduleExit(), "ModuleExit");
    runLoop.runFor (50);
    require (runLoop.fds.empty() && runLoop.timers.empty(), "plug-in unregistered every run-loop handler/timer");
    require (host.refs == 1 && frame.refs == 1 && handler.refs == 1 && runLoop.refs == 1, "host object references balanced after teardown");

    std::printf ("instances %d, blocks %d, note events %d, state round trips %d, views attached %d, view resizes %d, host resize requests %d, content scale factors %d, size negotiations %d\n",
                 instancesCreated, blocks, events, stateRoundTrips, views.attached, views.resized, frame.resizeRequests, views.scaleFactorsApplied, views.negotiations);
    std::printf ("run loop: fd registrations %d, fd callbacks %d, timer registrations %d, timer callbacks %d; component handler: begin %d perform %d end %d restart %d\n",
                 runLoop.fdRegistrations, runLoop.fdCallbacks, runLoop.timerRegistrations, runLoop.timerCallbacks, handler.begins, handler.performs, handler.ends, handler.restarts);
    std::printf ("%d check(s), %d failure(s): %s\n", checks, failures, failures == 0 ? "PASS" : "FAIL");
    std::fflush (stdout);
    if (display != nullptr) XCloseDisplay (display);
    if (__lsan_do_leak_check != nullptr)
    {
        std::printf ("leak check before dlclose (symbols still loaded)\n"); std::fflush (stdout);
        __lsan_do_leak_check();                                  // exits with the sanitizer's code when leaks exist
    }
    if (doDlclose) dlclose (lib);
    return failures == 0 ? 0 : 1;
}
