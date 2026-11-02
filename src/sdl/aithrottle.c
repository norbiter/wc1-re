/* Fork-only: --ai-throttle[=<ships>] stretches the fighters' think interval
 * when many of them are in flight.
 *
 * In 1996 a crowded fight dragged the frame rate down, and because the sim is
 * locked to the frame rate, the AI (and everything else) got fewer ticks per
 * real second.  This port holds the full frame rate, so large battles are
 * harder than they used to be.  Rather than slowing the whole sim, this only
 * defers each fighter's decision step (regulate_turn's "think now" moment).
 * Collision avoidance, flight physics, weapons and the player are untouched. */
#include "wc1.h"

#define AI_THROTTLE_DEFAULT_SHIPS 6
#define AI_THROTTLE_SHIPS_PER_STEP 3
#define AI_THROTTLE_MAX_PERIOD 4

static int g_bAiThrottleEnabled;
static int g_nAiThrottleShips = AI_THROTTLE_DEFAULT_SHIPS;
static int g_nAiThrottlePeriod = 1;

void SdlAiThrottleEnable(int ships)
{
    g_bAiThrottleEnabled = 1;
    g_nAiThrottleShips = ships > 0 ? ships : AI_THROTTLE_DEFAULT_SHIPS;
}

void SdlAiThrottleBeginTick(void)
{
    int obj;
    int ships;

    g_nAiThrottlePeriod = 1;
    if (!g_bAiThrottleEnabled)
        return;
    ships = 0;
    for (obj = 1; obj <= SPACE_LAST_MOVING_OBJECT; obj++) {
        if (aeObjectClass[obj] == OBJECT_CLASS_SHIP)
            ships++;
    }
    if (ships <= g_nAiThrottleShips)
        return;
    g_nAiThrottlePeriod = 2 +
        (ships - g_nAiThrottleShips - 1) / AI_THROTTLE_SHIPS_PER_STEP;
    if (g_nAiThrottlePeriod > AI_THROTTLE_MAX_PERIOD)
        g_nAiThrottlePeriod = AI_THROTTLE_MAX_PERIOD;
}

/* Nonzero when this fighter should skip its decision step this tick.  Ships
 * are staggered by object number so they do not all think on the same tick. */
int SdlAiThrottleDefers(short obj)
{
    if (g_nAiThrottlePeriod <= 1 || obj == 0 ||
        aeObjectClass[obj] != OBJECT_CLASS_SHIP)
        return 0;
    return (nCurrentSpaceFrameValue + obj) % g_nAiThrottlePeriod != 0;
}
