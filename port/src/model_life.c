/* Model-resource lifetime diagnostic; enable with GE_MODEL_LIFE=1.
 * The table deliberately lives outside MEMPOOL_STAGE. A reset can therefore
 * be compared with the generation of the last actual file load. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include <memp.h>
#include "game/chrobjdata.h"
#include "model_life.h"

extern MemoryPool g_mempPools[];

typedef struct ModelLifeEntry {
    ModelFileHeader *header;
    const char *name;
    void *filedata;
    ModelNode *loadedRoot;
    ModelNode **loadedSwitches;
    ModelFileTextures *loadedTextures;
    unsigned generation;
    unsigned reportedGeneration;
    int loaded;
    int explicitDestination;
    int reportedState;
} ModelLifeEntry;

static ModelLifeEntry s_entries[1024];
static unsigned s_count;
static unsigned s_generation;
static int s_enabled = -1;
static int s_botBody = -1;
static int s_botHead = -1;

/* Exported, stable symbols for tools_pc/model_watch.gdb. The debugger owns
 * watchpoints; these globals only identify the allocation and checkpoints. */
ModelNode *g_ModelWatchNode;
ModelFileHeader *g_ModelWatchHeader;
const char *g_ModelWatchPhase;
unsigned g_ModelWatchGeneration;
volatile unsigned g_ModelWatchSerial;

#if defined(__GNUC__)
__attribute__((noinline, used))
#endif
void modelLifeWatchBreak(void)
{
    ++g_ModelWatchSerial;
}

void modelLifeWatchCheckpoint(const char *phase, const char *name,
                              ModelFileHeader *header)
{
    const char *selected = getenv("GE_MODEL_WATCH");
    if (!selected || !*selected || !name || strcmp(selected, name) ||
        !header || !header->RootNode) return;
    g_ModelWatchNode = header->RootNode;
    g_ModelWatchHeader = header;
    g_ModelWatchPhase = phase;
    g_ModelWatchGeneration = s_generation;
    fprintf(stderr,
            "[MODEL-WATCH] gen=%u phase=%s name=%s header=%p node=%p "
            "data=%p child=%p serial=%u\n",
            s_generation, phase, name, (void *)header,
            (void *)g_ModelWatchNode, (void *)g_ModelWatchNode->Data,
            (void *)g_ModelWatchNode->Child, g_ModelWatchSerial + 1);
    fflush(stderr);
    modelLifeWatchBreak();
}

static int enabled(void)
{
    if (s_enabled < 0) {
        const char *value = getenv("GE_MODEL_LIFE");
        s_enabled = value && !strcmp(value, "1");
    }
    return s_enabled;
}

static ModelLifeEntry *entryFor(ModelFileHeader *header, int create)
{
    unsigned i;
    for (i = 0; i < s_count; ++i)
        if (s_entries[i].header == header) return &s_entries[i];
    if (!create || s_count == sizeof(s_entries) / sizeof(s_entries[0])) return NULL;
    s_entries[s_count].header = header;
    return &s_entries[s_count++];
}

static void identity(ModelFileHeader *header, char *label, size_t size)
{
    int i;
    for (i = 0; i < HEAD_END; ++i) {
        if (c_item_entries[i].header == header) {
            snprintf(label, size, "CHR:%d:%s", i, c_item_entries[i].filename);
            return;
        }
    }
    for (i = 0; i < 340; ++i) {
        if (PitemZ_entries[i].header == header) {
            snprintf(label, size, "PROP:%d:%s", i, PitemZ_entries[i].filename);
            return;
        }
    }
    snprintf(label, size, "LOCAL:%p", (void *)header);
}

static int persistent(ModelFileHeader *header)
{
    char label[96];
    identity(header, label, sizeof(label));
    return strncmp(label, "LOCAL:", 6) != 0;
}

static const char *bankOf(const void *pointer)
{
    uintptr_t p = (uintptr_t)pointer;
    if (p >= (uintptr_t)g_mempPools[MEMPOOL_STAGE].start &&
        p < (uintptr_t)g_mempPools[MEMPOOL_STAGE].end) return "STAGE";
    if (p >= (uintptr_t)g_mempPools[MEMPOOL_PERMANENT].start &&
        p < (uintptr_t)g_mempPools[MEMPOOL_PERMANENT].end) return "PERMANENT";
    return "OTHER";
}

static void report(const char *event, ModelFileHeader *header, ModelLifeEntry *entry,
                   const char *state)
{
    char label[96];
    if (!header) return;
    identity(header, label, sizeof(label));
    fprintf(stderr,
            "[MODEL-LIFE] gen=%u event=%s id=%s hdr=%p root=%p switches=%p textures=%p records=%d "
            "loaded_gen=%u loaded_root=%p data=%p bank=%s dst=%s state=%s\n",
            s_generation, event, label, (void *)header, (void *)header->RootNode,
            (void *)header->Switches, (void *)header->Textures, header->numRecords,
            entry ? entry->generation : 0, entry ? (void *)entry->loadedRoot : NULL,
            entry ? entry->filedata : NULL,
            entry ? bankOf(entry->filedata) : "UNKNOWN",
            entry ? (entry->explicitDestination ? "EXPLICIT" : "BANK_ALLOC") : "UNKNOWN",
            state);
    fflush(stderr);
}

void modelLifeEvent(const char *event, ModelFileHeader *header)
{
    if (!enabled() || !header) return;
    report(event, header, entryFor(header, 0), "OBSERVE");
}

void modelLifeLoaded(ModelFileHeader *header, const char *name, void *filedata,
                     int explicitDestination)
{
    ModelLifeEntry *entry;
    if (!enabled() || !header) return;
    /* A header copied into a stage allocation cannot outlive its bank.
     * Record global c_item/Pitem definitions, whose address survives reset. */
    if (!persistent(header)) {
        fprintf(stderr, "[MODEL-LIFE] gen=%u event=LOAD_LOCAL name=%s hdr=%p data=%p bank=%s dst=%s\n",
                s_generation, name, (void *)header, filedata, bankOf(filedata),
                explicitDestination ? "EXPLICIT" : "BANK_ALLOC");
        return;
    }
    entry = entryFor(header, 1);
    if (!entry) return;
    entry->name = name;
    entry->loaded = 1;
    entry->filedata = filedata;
    entry->loadedRoot = header->RootNode;
    entry->loadedSwitches = header->Switches;
    entry->loadedTextures = header->Textures;
    entry->generation = s_generation;
    entry->explicitDestination = explicitDestination;
    entry->reportedGeneration = 0;
    report("LOAD", header, entry, "FRESH");
}

void modelLifeValidate(const char *event, ModelFileHeader *header)
{
    ModelLifeEntry *entry;
    int state;
    if (!enabled() || !header) return;
    entry = entryFor(header, persistent(header));
    /* Stale generation, root replaced without a recorded load, unloaded, or
     * current generation. Do not dereference the backing node here. */
    state = !header->RootNode ? 1 : !entry || !entry->loaded ? 2 :
            entry->generation != s_generation ? 3 :
            entry->loadedRoot != header->RootNode ? 4 :
            entry->loadedSwitches != header->Switches ||
            entry->loadedTextures != header->Textures ? 6 : 5;
    if (entry && !strcmp(event, "HELD_WEAPON_RENDER") &&
        entry->reportedGeneration == s_generation && entry->reportedState == state)
        return; /* held-weapon rendering runs every frame */
    if (entry) {
        entry->reportedGeneration = s_generation;
        entry->reportedState = state;
    }
    report(event, header, entry, state == 1 ? "UNLOADED" :
           state == 2 ? "UNTRACKED" : state == 3 ? "STALE_GENERATION" :
           state == 4 ? "ROOT_CHANGED_WITHOUT_LOAD" :
           state == 6 ? "BACKING_CHANGED_WITHOUT_LOAD" : "CURRENT");
}

void modelLifeStageBegin(int stage)
{
    unsigned i;
    if (!enabled()) return;
    ++s_generation;
    fprintf(stderr, "[MODEL-LIFE] gen=%u event=STAGE_BEGIN stage=%d\n", s_generation, stage);
    for (i = 0; i < s_count; ++i)
        if (persistent(s_entries[i].header))
            report("PRE_BANK_RESET", s_entries[i].header, &s_entries[i], "OBSERVE");
    modelLifeEvent("TT33_PRE_BANK_RESET", PitemZ_entries[PROP_CHRTT33].header);
}

void modelLifeStageEnd(int stage)
{
    unsigned i;
    if (enabled()) {
        fprintf(stderr, "[MODEL-LIFE] gen=%u event=STAGE_END stage=%d\n", s_generation, stage);
        for (i = 0; i < s_count; ++i)
            if (persistent(s_entries[i].header))
                report("PRE_BANK_RELEASE", s_entries[i].header, &s_entries[i], "OBSERVE");
    }

    /* The next stage reuses these addresses. Tell GDB to retire hardware
     * watchpoints before zlib writes a different resource over this node. */
    if (g_ModelWatchNode) {
        g_ModelWatchPhase = "RETIRED";
        g_ModelWatchGeneration = s_generation;
        fprintf(stderr, "[MODEL-WATCH] gen=%u phase=RETIRED node=%p\n",
                s_generation, (void *)g_ModelWatchNode);
        fflush(stderr);
        modelLifeWatchBreak();
        g_ModelWatchNode = NULL;
    }
}

void modelLifeStageCleanup(void)
{
    if (!enabled()) return;
    modelLifeEvent("PRE_CLEANUP_TT33", PitemZ_entries[PROP_CHRTT33].header);
    if (s_botBody >= 0 && s_botBody < HEAD_END)
        modelLifeEvent("PRE_CLEANUP_BODY", c_item_entries[s_botBody].header);
    if (s_botHead >= 0 && s_botHead < HEAD_END)
        modelLifeEvent("PRE_CLEANUP_HEAD", c_item_entries[s_botHead].header);
}

void modelLifeBot(int body, int head)
{
    if (!enabled()) return;
    s_botBody = body;
    s_botHead = head;
    fprintf(stderr, "[MODEL-LIFE] gen=%u event=BOT_SELECT body=%d head=%d\n",
            s_generation, s_botBody, s_botHead);
    if (body >= 0 && body < HEAD_END)
        modelLifeValidate("BOT_BODY_BEFORE_SPAWN", c_item_entries[body].header);
    if (head >= 0 && head < HEAD_END)
        modelLifeValidate("BOT_HEAD_BEFORE_SPAWN", c_item_entries[head].header);
}

void modelLifeAttachment(const char *event, ModelFileHeader *body,
                         ModelFileHeader *head, void *placeholder,
                         void *child, void *parent)
{
    if (!enabled()) return;
    fprintf(stderr,
            "[MODEL-LIFE] gen=%u event=%s body_hdr=%p head_hdr=%p placeholder=%p "
            "placeholder_child=%p head_root_parent=%p\n",
            s_generation, event, (void *)body, (void *)head,
            placeholder, child, parent);
}
