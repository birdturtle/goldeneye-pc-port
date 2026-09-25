#ifndef GE_MP_COMBAT_H
#define GE_MP_COMBAT_H

struct ChrRecord;

/* MP players have one normalized health unit. Character weapon damage is
 * eight times the normalized player damage, so a full-health Simulant has
 * eight actor damage units. Negative actor damage represents armor. */
#define MP_COMBAT_ACTOR_HEALTH 8.0f

void mpCombatSpawnSimulant(int slot, struct ChrRecord *chr);
void mpCombatNoteSimulantHit(struct ChrRecord *chr, int human_index);
int mpCombatSimulantKeepsMoving(struct ChrRecord *chr);
int mpCombatShouldCountNativeKill(struct ChrRecord *chr);
void mpCombatSimulantDied(int slot, struct ChrRecord *chr);
void mpCombatDamageHuman(int attacker_slot, int victim_human, float damage,
                         float direction_x, float direction_z);

#endif
