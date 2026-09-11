// Multi-instance checks of SynthLib's VST3 wrapper, linked straight against it with a fake plug-in
// that records which instance every call reaches. Host classes after GenBridge's tools/vst3check.cpp.
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/gui/iplugview.h"

#include "synthlibPlugin.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static int gFailures = 0;

static void check(const char * what, bool ok) {
    printf("  %-60s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

// ---- the fake plug-in --------------------------------------------------------------------------

struct Fake {
    int     tag;
    double  values[2];
    int     sets;
    int     messages;
    int64_t lastMessage;
};

static Fake * gCreated[8];
static int    gCreatedCount = 0;

static void * fake_create(const tSynthLibPluginDesc *) {
    Fake * f = new Fake();
    f->tag = gCreatedCount + 1;
    gCreated[gCreatedCount++] = f;
    return f;
}
static void fake_destroy(void * inst) { delete (Fake *)inst; }
static void fake_set(void * inst, uint32_t id, double v) {
    Fake * f = (Fake *)inst;
    if (id < 2) { f->values[id] = v; f->sets++; }
}
static double fake_get(void * inst, uint32_t id) { return (id < 2) ? ((Fake *)inst)->values[id] : 0.0; }
static uint32_t fake_latency(void * inst) { return (uint32_t)(((Fake *)inst)->tag * 100); }
static void fake_message(void * inst, const char *, int64_t value) {
    Fake * f = (Fake *)inst;
    f->messages++;
    f->lastMessage = value;
}

static const tSynthLibParam gParams[2] = {
    { 0, "One", "One", eSynthLibUnitPercent, 0.0, 100.0, 0.0, 0, SYNTHLIB_MIDI_MOD_WHEEL, 0 },
    { 1, "Two", "Two", eSynthLibUnitPercent, 0.0, 100.0, 0.0, 0, SYNTHLIB_MIDI_NONE, 0 },
};
static const tSynthLibBus gOut[1] = { { "Out", 2, false, true } };
static const uint8_t gProc[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const uint8_t gCtrl[16] = { 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };

static tSynthLibPluginDesc make_desc(void) {
    tSynthLibPluginDesc d;
    memset(&d, 0, sizeof(d));
    d.name = "Fake"; d.vendor = "Test"; d.version = "0";
    d.isInstrument = true;
    d.outputs = gOut; d.numOutputs = 1;
    d.wantsMidiIn = true;
    d.controllerAppliesParams = true;
    d.vst3ProcessorUid = gProc; d.vst3ControllerUid = gCtrl;
    d.params = gParams; d.numParams = 2;
    d.cb.create = fake_create; d.cb.destroy = fake_destroy;
    d.cb.setParam = fake_set; d.cb.getParam = fake_get;
    d.cb.latencySamples = fake_latency; d.cb.message = fake_message;
    return d;
}
static const tSynthLibPluginDesc gDesc = make_desc();
static const tSynthLibPluginSet  gSet  = { &gDesc, 1 };

const tSynthLibPluginSet * synthlib_plugin_variants(void) { return &gSet; }
IPlugView * synthlib_vst3_create_view(const tSynthLibPluginDesc *, void *, FUnknown *, double, double *) {
    return nullptr;
}

extern "C" IPluginFactory * PLUGIN_API GetPluginFactory(void);

// ---- a minimal host ----------------------------------------------------------------------------

class Attributes : public IAttributeList {
public:
    std::map<std::string, int64> ints;
    tresult PLUGIN_API queryInterface(const TUID, void ** o) override { *o = nullptr; return kNoInterface; }
    uint32 PLUGIN_API addRef(void) override { return 1; }
    uint32 PLUGIN_API release(void) override { return 1; }
    tresult PLUGIN_API setInt(AttrID id, int64 value) override { ints[id] = value; return kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64 & value) override {
        auto it = ints.find(id);
        if (it == ints.end()) { return kResultFalse; }
        value = it->second;
        return kResultOk;
    }
    tresult PLUGIN_API setFloat(AttrID, double) override { return kResultFalse; }
    tresult PLUGIN_API getFloat(AttrID, double &) override { return kResultFalse; }
    tresult PLUGIN_API setString(AttrID, const TChar *) override { return kResultFalse; }
    tresult PLUGIN_API getString(AttrID, TChar *, uint32) override { return kResultFalse; }
    tresult PLUGIN_API setBinary(AttrID, const void *, uint32) override { return kResultFalse; }
    tresult PLUGIN_API getBinary(AttrID, const void *&, uint32 &) override { return kResultFalse; }
};

class Message final : public IMessage {
public:
    std::string id;
    Attributes  attrs;
    int32       rc = 1;
    tresult PLUGIN_API queryInterface(const TUID, void ** o) override { *o = nullptr; return kNoInterface; }
    uint32 PLUGIN_API addRef(void) override { return (uint32)++rc; }
    uint32 PLUGIN_API release(void) override {
        int32 c = --rc;
        if (c == 0) { delete this; return 0; }
        return (uint32)c;
    }
    FIDString PLUGIN_API getMessageID(void) override { return id.c_str(); }
    void PLUGIN_API setMessageID(FIDString newId) override { id = (newId != nullptr) ? newId : ""; }
    IAttributeList * PLUGIN_API getAttributes(void) override { return &attrs; }
};

class HostApp : public IHostApplication {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void ** o) override {
        QUERY_INTERFACE(iid, o, FUnknown::iid, IHostApplication)
        QUERY_INTERFACE(iid, o, IHostApplication::iid, IHostApplication)
        *o = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef(void) override { return 1; }
    uint32 PLUGIN_API release(void) override { return 1; }
    tresult PLUGIN_API getName(String128 name) override { name[0] = 0; return kResultOk; }
    tresult PLUGIN_API createInstance(TUID cid, TUID, void ** obj) override {
        if (memcmp(cid, IMessage::iid.toTUID(), sizeof(TUID)) == 0) {
            *obj = (IMessage *)new Message();
            return kResultOk;
        }
        *obj = nullptr;
        return kResultFalse;
    }
};

class Handler : public IComponentHandler {
public:
    int        edits = 0;
    ParamID    lastId = 9999;
    ParamValue lastValue = -1.0;
    int        restarts = 0;
    int32      restartFlags = 0;
    tresult PLUGIN_API queryInterface(const TUID, void ** o) override { *o = nullptr; return kNoInterface; }
    uint32 PLUGIN_API addRef(void) override { return 1; }
    uint32 PLUGIN_API release(void) override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override { edits++; lastId = id; lastValue = v; return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override { restarts++; restartFlags |= flags; return kResultOk; }
};

struct Pair {
    IComponent *      proc = nullptr;
    IEditController * ctrl = nullptr;
    Handler           handler;
};

static HostApp gHost;

static void make_pair(IPluginFactory * factory, Pair & p, bool connect) {
    TUID pc, cc;
    memcpy(pc, gProc, 16);
    memcpy(cc, gCtrl, 16);
    factory->createInstance(pc, IComponent::iid, (void **)&p.proc);
    factory->createInstance(cc, IEditController::iid, (void **)&p.ctrl);
    p.proc->initialize(&gHost);
    p.ctrl->initialize(&gHost);
    p.ctrl->setComponentHandler(&p.handler);

    if (connect) {
        IConnectionPoint * a = nullptr;
        IConnectionPoint * b = nullptr;
        p.proc->queryInterface(IConnectionPoint::iid, (void **)&a);
        p.ctrl->queryInterface(IConnectionPoint::iid, (void **)&b);
        a->connect(b);
        b->connect(a);
        a->release();
        b->release();
    }
}

static void free_pair(Pair & p) {
    p.ctrl->terminate();
    p.proc->terminate();
    p.ctrl->release();
    p.proc->release();
}

static uint32 latency_of(Pair & p) {
    IAudioProcessor * ap = nullptr;
    p.proc->queryInterface(IAudioProcessor::iid, (void **)&ap);
    uint32 l = ap->getLatencySamples();
    ap->release();
    return l;
}

static void * edit_from_thread(void * inst) {
    synthlib_plugin_param_edited(inst, 0, 0.9);
    return nullptr;
}

int main(void) {
    IPluginFactory * factory = GetPluginFactory();
    Pair             p1, p2, p3;

    printf("two connected pairs\n");
    make_pair(factory, p1, true);
    make_pair(factory, p2, true);
    Fake * f1 = gCreated[0];
    Fake * f2 = gCreated[1];

    p1.ctrl->setParamNormalized(0, 0.3);
    check("controller 1 edit reaches instance 1", fabs(f1->values[0] - 0.3) < 1e-9);
    check("controller 1 edit does not reach instance 2", f2->sets == 0);
    p2.ctrl->setParamNormalized(0, 0.7);
    check("controller 2 edit reaches instance 2", fabs(f2->values[0] - 0.7) < 1e-9);
    check("instance 1 unchanged by controller 2", fabs(f1->values[0] - 0.3) < 1e-9);

    synthlib_plugin_param_edited(f1, 1, 0.4);
    check("param_edited(inst 1) reaches handler 1", (p1.handler.edits == 1) && (p1.handler.lastId == 1)
          && (fabs(p1.handler.lastValue - 0.4) < 1e-9));
    check("param_edited(inst 1) does not reach handler 2", p2.handler.edits == 0);
    check("controller 1 now reports 0.4", fabs(p1.ctrl->getParamNormalized(1) - 0.4) < 1e-9);
    check("param_value(inst 1) is 0.4", fabs(synthlib_plugin_param_value(f1, 1) - 0.4) < 1e-9);
    check("param_value(inst 2) is unaffected", fabs(synthlib_plugin_param_value(f2, 1)) < 1e-9);

    synthlib_plugin_latency_changed(f2);
    check("latency_changed(inst 2) restarts handler 2 only",
          (p2.handler.restarts == 1) && (p2.handler.restartFlags == kLatencyChanged)
          && (p1.handler.restarts == 0));
    check("processor 1 latency 100", latency_of(p1) == 100);
    check("processor 2 latency 200", latency_of(p2) == 200);

    check("send_message(inst 1) returns true", synthlib_plugin_send_message(f1, "hello", 5));
    check("message arrives at instance 1 with its value", (f1->messages == 1) && (f1->lastMessage == 5));
    check("message did not arrive at instance 2", f2->messages == 0);

    pthread_t thread;
    pthread_create(&thread, nullptr, edit_from_thread, f2);
    pthread_join(thread, nullptr);
    int before = p2.handler.edits;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, false);
    check("an edit from a worker thread is posted, not run in place", before == 0);
    check("... and arrives at handler 2 on the main thread",
          (p2.handler.edits == 1) && (p2.handler.lastId == 0) && (fabs(p2.handler.lastValue - 0.9) < 1e-9));

    printf("a third, UNCONNECTED pair while two others live\n");
    make_pair(factory, p3, false);
    Fake * f3 = gCreated[2];
    int    s1 = f1->sets, s2 = f2->sets;
    p3.ctrl->setParamNormalized(0, 0.5);
    check("an unbound controller with 3 instances reaches none", (f1->sets == s1) && (f2->sets == s2) && (f3->sets == 0));
    synthlib_plugin_param_edited(f3, 0, 0.5);
    check("param_edited(inst 3) has no controller to reach", p3.handler.edits == 0);

    printf("the two connected pairs go away\n");
    free_pair(p1);
    free_pair(p2);
    p3.ctrl->setParamNormalized(0, 0.6);
    check("the unbound controller now falls back to the sole instance", fabs(f3->values[0] - 0.6) < 1e-9);
    synthlib_plugin_param_edited(f3, 1, 0.25);
    check("... and param_edited(inst 3) reaches it", (p3.handler.edits == 1) && (p3.handler.lastId == 1));
    free_pair(p3);

    printf("%s (%d failure%s)\n", (gFailures == 0) ? "ALL PASSED" : "FAILED", gFailures, (gFailures == 1) ? "" : "s");
    return (gFailures == 0) ? 0 : 1;
}
