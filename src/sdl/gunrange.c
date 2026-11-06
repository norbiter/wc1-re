/* Fork-only: --gun-range shows whether the target is inside effective gun
 * range by tinting the central crosshair: dark grey when out of range,
 * the normal sprite colours when in range.
 *
 * Effective range of one gun is how far its shot flies relative to the
 * target before it expires: (shot speed - target speed) * lifetime, with both
 * speeds taken along the player's forward (shot) direction. Shot speed is the
 * gun's muzzle speed plus the player's own velocity along that direction, the
 * way fire_weapon builds it. With "Full Guns" every enabled gun must reach. */
#include "wc1.h"

static int g_bGunRange;

void SdlGunRangeEnable(void)
{
    g_bGunRange = 1;
}

int SdlGunRangeActive(void)
{
    return g_bGunRange;
}

/* Nonzero when every enabled gun can reach the current target. */
static int SdlTargetInGunRange(void)
{
    const ShipWeaponSlot *weapons = (ShipWeaponSlot *)&aShipWeaponsTable[0][1];
    int target = acShipTargetTable[0];
    int count = aShipWeaponsTable[0][0];
    int ownAlong;
    int targetAlong;
    int distance;
    int weapon;
    int guns = 0;

    if (target < 0 || aeObjectClass[target] < OBJECT_CLASS_SHIP)
        return 0;
    ownAlong = vector_length_in_dir(&aShipVelocity[0], &aShipFwdVec[0]);
    targetAlong = vector_length_in_dir(&aShipVelocity[target], &aShipFwdVec[0]);
    distance = distance_from_object(0, (short)target);
    for (weapon = 0; weapon < count; weapon++) {
        const ObjectTypeData *type;
        int closing;

        if (weapons[weapon].disabled)
            continue;
        type = &aObjectTypeData[weapons[weapon].type];
        if (type->objectClass != OBJECT_CLASS_PROJECTILE)
            continue;
        guns++;
        /* 8.8 fixed units per tick, as in fire_weapon. */
        closing = (type->maximumVelocity << 8) + ownAlong - targetAlong;
        if (closing <= 0 ||
            (int64_t)closing * type->lifetime / 256 < distance)
            return 0;
    }
    return guns > 0;
}

/* Draws the crosshair sprite for the current range state. */
void SdlGunRangeDrawCrosshair(struct Viewport *viewport, short x, short y,
                              unsigned char *shape)
{
    if (SdlTargetInGunRange())
        DrawSpriteDefault(viewport, x, y, shape, 0);
    else
        DrawSolidColourSprite(viewport, x, y, shape, 0, cDarkGreyColour);
}
