#ifndef GE_MP_ROSTER_H
#define GE_MP_ROSTER_H

/* Match identities are limited to four. Each slot has a score-data row;
 * only human entries own a struct player, controller, or viewport.
 * A Simulant's chrnum is resolved in the current
 * stage; no stage-owned character or prop pointer is retained here. */
#define MP_ROSTER_MAX 4

typedef enum MpRosterKind {
    MP_ROSTER_EMPTY = 0,
    MP_ROSTER_HUMAN,
    MP_ROSTER_SIMULANT
} MpRosterKind;

typedef struct MpRosterRef {
    unsigned generation;
    unsigned incarnation;
    int slot;
} MpRosterRef;

typedef struct MpRosterEntry {
    MpRosterKind kind;
    int human_index; /* -1 for a Simulant */
    int chrnum;      /* -1 until a Simulant actor is present */
    unsigned incarnation;
} MpRosterEntry;

/* A failed configuration leaves the roster inactive. Human slots always
 * precede Simulants, and human_count + simulant_count never exceeds four. */
int mpRosterBegin(int stage, int human_count, int simulant_count);
void mpRosterEnd(void);
int mpRosterStage(void);
int mpRosterCount(void);
int mpRosterHumanCount(void);
const MpRosterEntry *mpRosterEntry(int slot);

/* Bind a stage actor to its reserved Simulant entry. Respawning increments
 * the incarnation, invalidating references to the old actor. */
int mpRosterBindSimulant(int slot, int chrnum);
void mpRosterUnbindSimulant(int slot, int chrnum);
MpRosterRef mpRosterRefForSlot(int slot);
int mpRosterResolve(MpRosterRef ref);
int mpRosterSlotForHuman(int human_index);
int mpRosterSlotForChr(int chrnum);

#endif
