#include <ultra64.h>
#include <memp.h>
#include "player.h"
#include "bondinv.h"
#include "inititemslots.h"

void reinit_gunheld_totaltime(void) {
    s32 i;
  
    g_CurrentPlayer->equipallguns = FALSE;
    
    for (i = 0; i != 10; i++) {
        g_CurrentPlayer->gunheldarr[i].totaltime = -1;
    }
}

void alloc_additional_item_slots(s32 additionalentries) {
  g_CurrentPlayer->equipmaxitems = additionalentries + 0x1e;
#ifdef PORT
    /* D323: InvItem grows from the N64's 0x14 to 0x20 on x86-64 because its
     * next/prev pointers and prop union member widen. The old 0x14 stride
     * under-allocated this stage block; bondinvReinitInv's real InvItem
     * stride then wrote -1 into the next allocation on MP respawn. */
    g_CurrentPlayer->p_itemcur = mempAllocBytesInBank(
        (g_CurrentPlayer->equipmaxitems * sizeof(*g_CurrentPlayer->p_itemcur) + 0xfU) & ~0xfULL,
        MEMPOOL_STAGE);
#else
    g_CurrentPlayer->p_itemcur     = mempAllocBytesInBank((g_CurrentPlayer->equipmaxitems * 0x14 + 0xfU | 0xf) ^ 0xf, MEMPOOL_STAGE);
#endif
  bondinvReinitInv();
}
