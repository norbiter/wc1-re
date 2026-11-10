/* Fork-only: --fair-asteroids stops the asteroid field from spawning rocks
 * on an exact intercept course. Retail's init_hazard aims some rocks spawned
 * in view at where the player will be after their flight time, so holding
 * course means a hit; with this switch those rocks drift like the others. */
#include "wc1.h"

static int g_bFairAsteroids;

void SdlFairAsteroidsEnable(void)
{
    g_bFairAsteroids = 1;
}

int SdlFairAsteroidsEnabled(void)
{
    return g_bFairAsteroids;
}
