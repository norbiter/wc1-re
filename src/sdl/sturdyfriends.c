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
