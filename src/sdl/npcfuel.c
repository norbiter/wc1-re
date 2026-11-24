/* Fork-only: fuel matters to wingmen and enemies, not just the player.
 *
 * Every ship spawns with its type's rated fuel and burns it (5 a tick in
 * flight, 200 a tick on afterburner), but the original never acts on it for
 * anyone but the player: only your_afterburner() checks the gauge, and
 * fire_afterburner(), which the AI calls, does not.  So a Salthi can always
 * outrun a Scimitar by burning forever.
 *
 * --npc-fuel (on by default) gives every other ship the player's rule: with
 * an empty tank it cannot light its afterburner, and a burn in progress ends
 * the tick the tank runs dry.
 *
 * --low-enemy-fuel (--easier) spawns Kilrathi ships with a quarter of their
 * rated fuel.  An enemy usually flies one engagement while the player and
 * wingmen fly several, so a full load rarely runs out. */
#include "wc1.h"

#define LOW_ENEMY_FUEL_DIVISOR 4

static int g_bNpcFuel;
static int g_bLowEnemyFuel;

void SdlNpcFuelEnable(void)
{
    g_bNpcFuel = 1;
}

void SdlLowEnemyFuelEnable(void)
{
    g_bLowEnemyFuel = 1;
}

/* Called from set_objects_data right after the type's fuel is loaded. */
void SdlLowEnemyFuelTrim(short obj, short type)
{
    if (!g_bLowEnemyFuel || obj <= 0 || obj >= 10 ||
        type < OBJECT_TYPE_SALTHI || type > OBJECT_TYPE_KILRATHI_BASE)
        return;
    anShipFuelsTable[obj] = aObjectTypeData[type].lifetime /
        LOW_ENEMY_FUEL_DIVISOR;
}

/* Nonzero when a ship other than the player is out of fuel and so may not
 * start or extend an afterburner burn. */
int SdlNpcFuelEmpty(short obj)
{
    return g_bNpcFuel && obj > 0 && obj < 10 && !SdlSwcFlightActive() &&
        aeObjectClass[obj] >= OBJECT_CLASS_SHIP &&
        anShipFuelsTable[obj] <= 0;
}

/* Called once per tick of an afterburner burn, before its timer counts down:
 * a dry tank cuts the timer so the burn ends this tick. */
void SdlNpcFuelAfterburnerTick(short obj)
{
    if (SdlNpcFuelEmpty(obj) && asShipAfterburnerTimer[obj] > 1)
        asShipAfterburnerTimer[obj] = 1;
}
