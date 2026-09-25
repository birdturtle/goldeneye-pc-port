#include "mp_roster.h"

static struct {
    unsigned generation;
    int stage;
    int count;
    int humans;
    MpRosterEntry entries[MP_ROSTER_MAX];
} s_roster;

static void mpRosterClear(void)
{
    s_roster.stage = -1;
    s_roster.count = 0;
    s_roster.humans = 0;
    for (int i = 0; i < MP_ROSTER_MAX; ++i) {
        s_roster.entries[i].kind = MP_ROSTER_EMPTY;
        s_roster.entries[i].human_index = -1;
        s_roster.entries[i].chrnum = -1;
        s_roster.entries[i].incarnation = 0;
    }
}

static void mpRosterNextGeneration(void)
{
    if (++s_roster.generation == 0) ++s_roster.generation;
}

int mpRosterBegin(int stage, int human_count, int simulant_count)
{
    mpRosterNextGeneration();
    mpRosterClear();
    if (stage <= 0 || human_count < 1 || human_count > MP_ROSTER_MAX ||
        simulant_count < 0 || simulant_count > MP_ROSTER_MAX - human_count)
        return 0;

    s_roster.stage = stage;
    s_roster.humans = human_count;
    s_roster.count = human_count + simulant_count;
    for (int i = 0; i < s_roster.count; ++i) {
        MpRosterEntry *entry = &s_roster.entries[i];
        entry->kind = i < human_count ? MP_ROSTER_HUMAN : MP_ROSTER_SIMULANT;
        entry->human_index = i < human_count ? i : -1;
        entry->incarnation = i < human_count ? 1 : 0;
    }
    return 1;
}

void mpRosterEnd(void)
{
    mpRosterNextGeneration();
    mpRosterClear();
}

int mpRosterStage(void) { return s_roster.stage; }
int mpRosterCount(void) { return s_roster.count; }
int mpRosterHumanCount(void) { return s_roster.humans; }

const MpRosterEntry *mpRosterEntry(int slot)
{
    return slot >= 0 && slot < s_roster.count ? &s_roster.entries[slot] : 0;
}

static MpRosterEntry *mpRosterMutable(int slot)
{
    return slot >= 0 && slot < s_roster.count ? &s_roster.entries[slot] : 0;
}

int mpRosterBindSimulant(int slot, int chrnum)
{
    MpRosterEntry *entry;
    if (chrnum < 0 || !(entry = mpRosterMutable(slot)) ||
        entry->kind != MP_ROSTER_SIMULANT || entry->chrnum >= 0 ||
        mpRosterSlotForChr(chrnum) >= 0)
        return 0;
    entry->chrnum = chrnum;
    if (++entry->incarnation == 0) ++entry->incarnation;
    return 1;
}

void mpRosterUnbindSimulant(int slot, int chrnum)
{
    MpRosterEntry *entry = mpRosterMutable(slot);
    if (entry && entry->kind == MP_ROSTER_SIMULANT && entry->chrnum == chrnum)
        entry->chrnum = -1;
}

MpRosterRef mpRosterRefForSlot(int slot)
{
    MpRosterRef ref = {0, 0, -1};
    const MpRosterEntry *entry = mpRosterEntry(slot);
    if (entry && (entry->kind == MP_ROSTER_HUMAN || entry->chrnum >= 0)) {
        ref.generation = s_roster.generation;
        ref.incarnation = entry->incarnation;
        ref.slot = slot;
    }
    return ref;
}

int mpRosterResolve(MpRosterRef ref)
{
    const MpRosterEntry *entry = mpRosterEntry(ref.slot);
    return ref.generation != 0 && ref.generation == s_roster.generation &&
        entry && ref.incarnation != 0 && ref.incarnation == entry->incarnation &&
        (entry->kind == MP_ROSTER_HUMAN || entry->chrnum >= 0) ? ref.slot : -1;
}

int mpRosterSlotForHuman(int human_index)
{
    return human_index >= 0 && human_index < s_roster.humans ? human_index : -1;
}

int mpRosterSlotForChr(int chrnum)
{
    if (chrnum < 0) return -1;
    for (int i = s_roster.humans; i < s_roster.count; ++i)
        if (s_roster.entries[i].chrnum == chrnum) return i;
    return -1;
}
