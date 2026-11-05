/* Fork-only: --low-missile trims every Kilrathi ship to at most one missile
 * of each loaded type when it spawns, so early engagements are less about
 * baiting out missiles. */
#include "wc1.h"

static int g_bLowMissile;

void SdlLowMissileEnable(void)
{
    g_bLowMissile = 1;
}

/* Called right after a ship's loadout is copied into aShipWeaponsTable[obj]
 * (and, in SWC flight, its ammo into aSwcWeaponAmmo[obj]). */
void SdlLowMissileTrim(short obj, short type)
{
    ShipWeaponSlot *slots;
    int count;
    int in;
    int out = 0;
    int other;
    int swc;

    if (!g_bLowMissile || obj < 0 || obj >= 10 ||
        type < OBJECT_TYPE_SALTHI || type > OBJECT_TYPE_KILRATHI_BASE)
        return;
    slots = (ShipWeaponSlot *)&aShipWeaponsTable[obj][1];
    count = aShipWeaponsTable[obj][0];
    if (count > 10)
        return;
    swc = SdlSwcFlightActive();
    for (in = 0; in < count; in++) {
        int duplicate = 0;

        if (aObjectTypeData[slots[in].type].objectClass ==
                OBJECT_CLASS_MISSILE) {
            for (other = 0; other < out; other++)
                if (slots[other].type == slots[in].type)
                    duplicate = 1;
        }
        if (duplicate)
            continue;
        if (swc) {
            aSwcWeaponAmmo[obj][out] = aSwcWeaponAmmo[obj][in];
            if (aObjectTypeData[slots[in].type].objectClass ==
                    OBJECT_CLASS_MISSILE &&
                aSwcWeaponAmmo[obj][out] > 1)
                aSwcWeaponAmmo[obj][out] = 1;
        }
        slots[out++] = slots[in];
    }
    for (in = out; in < count; in++) {
        memset(&slots[in], 0, sizeof(slots[in]));
        if (swc)
            aSwcWeaponAmmo[obj][in] = 0;
    }
    aShipWeaponsTable[obj][0] = (signed char)out;
}
