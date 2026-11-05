/* Fork-only: --ff makes a gun shot or missile that touches a ship on its
 * shooter's own side vanish without damage, force or hit effects, spares
 * same-side ships from blast damage (missile, flak and ship explosions) and
 * from ramming damage (the collision itself still happens). */
#include "wc1.h"

static int g_bNoFriendlyFire;

void SdlNoFriendlyFireEnable(void)
{
    g_bNoFriendlyFire = 1;
}

/* Nonzero when attacker's damage to victim is friendly fire to suppress. */
int SdlNoFriendlyFireSpares(short attacker, short victim)
{
    if (!g_bNoFriendlyFire)
        return 0;
    /* aeShipSide only covers the first 12 objects; anything beyond (capital
       ships, turrets, mines) has no known side, so it is never spared. */
    if (attacker < 0 || attacker >= (short)SDL_arraysize(aeShipSide) ||
        victim < 0 || victim >= (short)SDL_arraysize(aeShipSide) ||
        attacker == victim)
        return 0;
    if (aeObjectClass[victim] < OBJECT_CLASS_SHIP)
        return 0;
    return aeShipSide[attacker] == aeShipSide[victim];
}

/* Nonzero when a shot or missile obj touching partner should just vanish. */
int SdlNoFriendlyFireBlocks(short obj, short partner)
{
    return SdlNoFriendlyFireSpares((short)acObjOwnersTable[obj], partner);
}
