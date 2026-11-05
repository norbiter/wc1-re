/* Fork-only: --sturdy-friends doubles the shields of friendly transports and
 * capital ships when they spawn, so escort and protection missions don't fall
 * apart before the player arrives. */
#include "wc1.h"

static int g_bSturdyFriends;

void SdlSturdyFriendsEnable(void)
{
    g_bSturdyFriends = 1;
}

static short DoubleShield(short value)
{
    int doubled = value * 2;

    return (short)(doubled > 32767 ? 32767 : doubled);
}

/* Called from set_objects_data right after the type's shields are applied. */
void SdlSturdyFriendsBoost(short obj, short type)
{
    int facing;

    if (!g_bSturdyFriends || obj < 0 ||
        type < OBJECT_TYPE_VENTURE || type > OBJECT_TYPE_TIGERS_CLAW)
        return;
    for (facing = 0; facing < 2; facing++) {
        aasShipShield[obj][facing] = DoubleShield(aasShipShield[obj][facing]);
        aasShipMaximumShieldTable[obj][facing] =
            DoubleShield(aasShipMaximumShieldTable[obj][facing]);
    }
}

/* Fork fix: abandoned() blows up any ship that wanders into a hostile nav
 * sphere while the player is far away. Mission 7-1's second transport does
 * exactly that on its way to the jump point, so it was scored as lost even
 * though the player saved it. Ships that a protect/escort objective (types
 * 2 and 3) refers to are exempt. */
int SdlIsProtectedObjectiveShip(short obj)
{
    short objective;

    for (objective = 0; objective < cMissionObjectiveCountValue; objective++) {
        int type = aMissionObjectivesArray[objective].type;

        if ((type == 2 || type == 3) &&
            aMissionObjectivesArray[objective].index == nShipMissionIndices[obj])
            return 1;
    }
    return 0;
}
