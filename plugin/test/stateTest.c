// Offline checks of synthlibPluginState.c: SLP2 round trip, SLP1 and legacy reads, id lookup.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "synthlibPluginState.h"

static const tSynthLibParam gTable[4] = {
    { 0, "A", "A", eSynthLibUnitPercent, 0.0, 100.0, 0.25, 0, SYNTHLIB_MIDI_NONE, 0 },
    { 1, "B", "B", eSynthLibUnitPercent, 0.0, 100.0, 0.50, 0, SYNTHLIB_MIDI_NONE, 0 },
    { 2, "C", "C", eSynthLibUnitPercent, 0.0, 100.0, 0.75, 0, SYNTHLIB_MIDI_NONE, SYNTHLIB_PARAM_NO_SAVE },
    { 3, "D", "D", eSynthLibUnitPercent, 0.0, 100.0, 1.00, 0, SYNTHLIB_MIDI_NONE, 0 },
};

static const char * gBlob = "hello-plugin";

static size_t get_state(void * inst, void * out, size_t len) {
    size_t need = strlen(gBlob);

    (void)inst;
    if ((out != NULL) && (len >= need)) {
        memcpy(out, gBlob, need);
    }
    return need;
}

// Dynamic list with gaps in the ids.
static const uint32_t gDynIds[5] = { 0, 1, 1000, 1001, 3079 };

static uint32_t dyn_count(const tSynthLibPluginDesc * d, void * inst) {
    (void)d; (void)inst;
    return 5;
}

static bool dyn_info(const tSynthLibPluginDesc * d, void * inst, uint32_t index, tSynthLibParamDesc * out) {
    (void)d; (void)inst;
    if (index >= 5) {
        return false;
    }
    out->id                = gDynIds[index];
    out->defaultNormalized = 0.1 * (double)index;
    out->plainMax          = 1.0;
    out->flags             = (index >= 2) ? (SYNTHLIB_PARAM_HIDDEN | SYNTHLIB_PARAM_NO_SAVE) : 0u;
    snprintf(out->title, sizeof(out->title), "P%u", gDynIds[index]);
    return true;
}

int main(void) {
    tSynthLibPluginDesc desc;
    tSynthLibParamStore store;

    memset(&desc, 0, sizeof(desc));
    desc.params      = gTable;
    desc.numParams   = 4;
    desc.cb.getState = get_state;

    assert(synthlib_params_init(&store, &desc, NULL));
    assert(store.identity && store.count == 4);
    assert(synthlib_params_get(&store, 1) == 0.5);
    assert(synthlib_params_set(&store, 1, 0.9));
    assert(synthlib_params_set(&store, 3, 7.0));            // clamped
    assert(synthlib_params_get(&store, 3) == 1.0);
    assert(!synthlib_params_set(&store, 4, 0.1));           // unknown id

    // SLP2 round trip
    uint8_t buf[512];
    size_t  need = synthlib_state_write(&desc, NULL, &store, NULL, 0);
    size_t  wrote = synthlib_state_write(&desc, NULL, &store, buf, sizeof(buf));

    assert(need == wrote);
    assert(memcmp(buf, "SLP2", 4) == 0);
    assert(wrote == 8 + 3 * 12 + 4 + strlen(gBlob));       // NO_SAVE excluded

    tSynthLibParamValue vals[8];
    uint32_t            n = 0;
    const void *        pd = NULL;
    size_t              pl = 0;

    assert(synthlib_state_read(&store, buf, wrote, vals, 8, &n, &pd, &pl));
    assert(n == 3);
    assert(vals[0].id == 0 && vals[0].value == 0.25);
    assert(vals[1].id == 1 && vals[1].value == 0.9);
    assert(vals[2].id == 3 && vals[2].value == 1.0);
    assert(pl == strlen(gBlob) && memcmp(pd, gBlob, pl) == 0);

    // An SLP2 blob from a build that still saved id 2: skipped the same way
    {
        uint8_t  older[64];
        uint32_t two = 2, idA = 0, idB = 2, none = 0;
        double   a = 0.5, b = 0.75;

        memcpy(older, "SLP2", 4);
        memcpy(older + 4, &two, 4);
        memcpy(older + 8, &idA, 4);
        memcpy(older + 12, &a, 8);
        memcpy(older + 20, &idB, 4);
        memcpy(older + 24, &b, 8);
        memcpy(older + 32, &none, 4);
        assert(synthlib_state_read(&store, older, 36, vals, 8, &n, &pd, &pl));
        assert(n == 1 && vals[0].id == 0 && vals[0].value == 0.5 && pl == 0);
    }

    // Truncated mid-record
    assert(!synthlib_state_read(&store, buf, 8 + 12 + 3, vals, 8, &n, &pd, &pl) || n <= 1);

    // SLP1 blob: 4 doubles by position, then plug-in blob
    uint8_t  old[128];
    double   v1[4] = { 0.1, 0.2, 0.3, 0.4 };
    uint32_t count = 4, blen = 3;

    memcpy(old, "SLP1", 4);
    memcpy(old + 4, &count, 4);
    memcpy(old + 8, v1, sizeof(v1));
    memcpy(old + 40, &blen, 4);
    memcpy(old + 44, "abc", 3);
    assert(synthlib_state_read(&store, old, 47, vals, 8, &n, &pd, &pl));
    // id 2 is NO_SAVE: an old project's value for it is not restored
    assert(n == 3 && vals[1].id == 1 && vals[1].value == 0.2 && vals[2].id == 3 && vals[2].value == 0.4);
    assert(pl == 3 && memcmp(pd, "abc", 3) == 0);

    // Legacy blob: no magic
    assert(synthlib_state_read(&store, "/Users/x/p.pch2", 15, vals, 8, &n, &pd, &pl));
    assert(n == 0 && pl == 15);

    // Corrupt count
    uint32_t huge = 1000000;

    memcpy(old + 4, &huge, 4);
    assert(!synthlib_state_read(&store, old, 47, vals, 8, &n, &pd, &pl));
    synthlib_params_free(&store);

    // Dynamic ids with gaps
    memset(&desc, 0, sizeof(desc));
    desc.cb.paramCount = dyn_count;
    desc.cb.paramInfo  = dyn_info;
    assert(synthlib_params_init(&store, &desc, NULL));
    assert(!store.identity);
    assert(synthlib_params_index(&store, 1000) == 2);
    assert(synthlib_params_index(&store, 3079) == 4);
    assert(synthlib_params_index(&store, 2) == -1);
    assert(synthlib_params_set(&store, 1001, 0.66) && synthlib_params_get(&store, 1001) == 0.66);
    wrote = synthlib_state_write(&desc, NULL, &store, buf, sizeof(buf));
    assert(wrote == 8 + 2 * 12 + 4);                        // only ids 0 and 1 saved

    // An SLP2 record for an id this build lacks is skipped
    uint32_t gone = 999;

    memcpy(buf + 8 + 12, &gone, 4);
    assert(synthlib_state_read(&store, buf, wrote, vals, 8, &n, &pd, &pl));
    assert(n == 1 && vals[0].id == 0);
    synthlib_params_free(&store);

    // A plug-in that saves nothing through the wrapper gets its own bytes back, unwrapped - and they
    // read back through the pre-header path, whole.
    static const tSynthLibParam gNothingSaved[1] = {
        { 0, "X", "X", eSynthLibUnitGeneric, 0.0, 1.0, 0.0, 0, SYNTHLIB_MIDI_NONE, SYNTHLIB_PARAM_NO_SAVE },
    };

    memset(&desc, 0, sizeof(desc));
    desc.params      = gNothingSaved;
    desc.numParams   = 1;
    desc.cb.getState = get_state;
    assert(synthlib_params_init(&store, &desc, NULL));
    need  = synthlib_state_write(&desc, NULL, &store, NULL, 0);
    wrote = synthlib_state_write(&desc, NULL, &store, buf, sizeof(buf));
    assert((need == strlen(gBlob)) && (wrote == need) && (memcmp(buf, gBlob, wrote) == 0));
    assert(synthlib_state_read(&store, buf, wrote, vals, 8, &n, &pd, &pl));
    assert((n == 0) && (pl == wrote) && (memcmp(pd, gBlob, pl) == 0));
    synthlib_params_free(&store);

    printf("state tests passed\n");
    return 0;
}
