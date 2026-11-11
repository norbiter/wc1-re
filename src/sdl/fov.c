/* Fork-only: --fov=DEGREES changes the field of view of cockpit-less
 * flight.
 *
 * The game projects with a focal length of half the view width, 160 pixels
 * for the 320-column screen, which is 90 degrees across those columns.  The
 * option scales that focal length; everything that projects a point or
 * sizes a sprite asks here for it, so sprites, 3D ships and the overlays
 * drawn over them stay in step.  The cockpit views keep the original
 * perspective, since their art was drawn for it.
 *
 * Two things the game measures on the screen are kept as they were at the
 * original focal length, so that the option changes the picture and not the
 * game: the range at which an object becomes too small to draw (which is
 * also where it stops being targetable), and the angle off the nose within
 * which a missile keeps its lock. */
#include "wc1.h"

#include <math.h>

#define FOV_ORIGINAL_DEGREES 90
#define FOV_MIN_DEGREES 60
#define FOV_MAX_DEGREES 140

/* Focal length as a multiple of the original one. */
static double g_dFocalScale = 1.0;

void SdlFovSet(int degrees)
{
    if (degrees < FOV_MIN_DEGREES)
        degrees = FOV_MIN_DEGREES;
    if (degrees > FOV_MAX_DEGREES)
        degrees = FOV_MAX_DEGREES;
    if (degrees == FOV_ORIGINAL_DEGREES)
        g_dFocalScale = 1.0;
    else
        g_dFocalScale =
            1.0 / tan((double)degrees * 3.14159265358979323846 / 360.0);
}

static double FocalScale(void)
{
    return bCockpitlessView > 0 ? g_dFocalScale : 1.0;
}

/* The focal length in 1/256 pixels: what the game writes as
 * (short)(nScreenWidth & ~1) << 7. */
int SdlFovFocalFixed(void)
{
    int focal = (short)(nScreenWidth & ~1) << 7;
    double scale = FocalScale();

    if (scale == 1.0)
        return focal;
    return (int)((double)focal * scale + 0.5);
}

/* The focal length in pixels. */
float SdlFovFocal(void)
{
    return (float)((double)(nScreenWidth & ~1) * 0.5 * FocalScale());
}

/* transform_objects_to_your_view drops an object whose screen scale is 4 or
 * less.  Given the scale at the focal length in use, return a scale that is
 * dropped only where the original focal length would have dropped it too:
 * an object the wider view alone has made too small stays a speck. */
int SdlFovKeepVisibleScale(int screenScale, int objectScale, int distance)
{
    int original;

    if ((unsigned short)screenScale > 4 || FocalScale() >= 1.0)
        return screenScale;
    original = MultiplyFixed(
        objectScale,
        DivideFixed((short)(nScreenWidth & ~1) << 15, distance)) >> 8;
    if ((unsigned short)original > 4)
        return 5;
    return screenScale;
}

/* target_locking drops a missile lock once the target is more than 60
 * pixels from the view centre; this is that limit, squared, for the focal
 * length in use. */
int SdlFovLockRadiusSquared(void)
{
    double scale = FocalScale();

    return (int)(0xe10 * scale * scale + 0.5);
}
