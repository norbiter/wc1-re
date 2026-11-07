/* Fork-only: --shield-flash briefly flashes a semitransparent blue hemisphere
 * over a ship whose shields absorb a hit.  The hemisphere faces the side the
 * shot came from, follows the ship as it turns, and fades out in a fraction
 * of a second.
 *
 * Like --hitspheres the effect is blended into the 8-bit indexed space
 * buffer through lookup tables from every palette index to the palette index
 * nearest its colour mixed with the flash colour.  There is one table per
 * quantised opacity level; they are rebuilt lazily whenever the palette
 * changes. */
#include "wc1.h"

#include "video_internal.h"

#define SHIELD_FLASH_MS 300
#define SHIELD_FLASH_LEVELS 5
#define SHIELD_FLASH_PEAK_ALPHA 0.60f

typedef struct ShieldFlash {
    Uint32 startTicks;
    int active;
    /* Unit direction from the ship to the impact, on the ship's own axes
     * (right, up, forward), so it turns with the ship. */
    float local[3];
} ShieldFlash;

static int g_bShieldFlashEnabled;
static ShieldFlash g_aShieldFlashes[SPACE_OBJECT_COUNT];
static unsigned char g_abShieldFlashQuads[1024];
static unsigned char g_aabShieldFlashTables[SHIELD_FLASH_LEVELS][256];
static int g_abShieldFlashTableValid[SHIELD_FLASH_LEVELS];
static int g_bShieldFlashPaletteValid;

static const float g_afShieldFlashColour[3] = { 110.0f, 190.0f, 255.0f };

void SdlShieldFlashEnable(void)
{
    g_bShieldFlashEnabled = 1;
}

int SdlShieldFlashEnabled(void)
{
    return g_bShieldFlashEnabled;
}

static float DotAxis(const FixedVector *a, const FixedVector *b)
{
    return (float)a->x * (float)b->x + (float)a->y * (float)b->y +
           (float)a->z * (float)b->z;
}

/* Called by inflict_damage when the victim's shield was up for the hit.
 * attacker is the projectile or ship that caused it, or -1. */
void SdlShieldFlashStart(short attacker, short victim)
{
    ShieldFlash *flash;
    FixedVector delta;
    float world[3];
    float length;

    if (!g_bShieldFlashEnabled || victim < 0 || victim >= SPACE_OBJECT_COUNT ||
        attacker < 0 || attacker >= SPACE_OBJECT_COUNT || attacker == victim)
        return;
    ComputeVectorDelta(&aShipPositionsArray[victim],
                       &aShipPositionsArray[attacker], &delta);
    world[0] = (float)delta.x;
    world[1] = (float)delta.y;
    world[2] = (float)delta.z;
    length = SDL_sqrtf(world[0] * world[0] + world[1] * world[1] +
                       world[2] * world[2]);
    if (length <= 0.0f)
        return;
    flash = &g_aShieldFlashes[victim];
    {
        FixedVector unit;

        unit.x = (int)(world[0] / length * 65536.0f);
        unit.y = (int)(world[1] / length * 65536.0f);
        unit.z = (int)(world[2] / length * 65536.0f);
        /* Axis vectors share one scale, so only the ratio matters. */
        flash->local[0] = DotAxis(&unit, &aShipRightVector[victim]);
        flash->local[1] = DotAxis(&unit, &aShipUpVector[victim]);
        flash->local[2] = DotAxis(&unit, &aShipFwdVec[victim]);
    }
    length = SDL_sqrtf(flash->local[0] * flash->local[0] +
                       flash->local[1] * flash->local[1] +
                       flash->local[2] * flash->local[2]);
    if (length <= 0.0f)
        return;
    flash->local[0] /= length;
    flash->local[1] /= length;
    flash->local[2] /= length;
    flash->startTicks = SDL_GetTicks();
    flash->active = 1;
}

static unsigned char FindShieldFlashColour(const unsigned char *quads,
                                           float red, float green, float blue)
{
    int best = 0;
    float bestDistance = 3.0e38f;
    int index;

    for (index = 0; index < 256; index++) {
        float dr = quads[index * 4 + 2] - red;
        float dg = quads[index * 4 + 1] - green;
        float db = quads[index * 4] - blue;
        float distance = dr * dr + dg * dg + db * db;

        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return (unsigned char)best;
}

/* Returns the table for opacity level 1..SHIELD_FLASH_LEVELS. */
static const unsigned char *GetShieldFlashTable(int level)
{
    const unsigned char *quads = SdlGetPaletteQuads();
    int slot = level - 1;
    float alpha;
    int index;

    if (!g_bShieldFlashPaletteValid ||
        SDL_memcmp(quads, g_abShieldFlashQuads,
                   sizeof(g_abShieldFlashQuads)) != 0) {
        SDL_memcpy(g_abShieldFlashQuads, quads, sizeof(g_abShieldFlashQuads));
        SDL_memset(g_abShieldFlashTableValid, 0,
                   sizeof(g_abShieldFlashTableValid));
        g_bShieldFlashPaletteValid = 1;
    }
    if (g_abShieldFlashTableValid[slot])
        return g_aabShieldFlashTables[slot];
    alpha = SHIELD_FLASH_PEAK_ALPHA * (float)level / SHIELD_FLASH_LEVELS;
    for (index = 0; index < 256; index++) {
        float red = g_abShieldFlashQuads[index * 4 + 2];
        float green = g_abShieldFlashQuads[index * 4 + 1];
        float blue = g_abShieldFlashQuads[index * 4];

        g_aabShieldFlashTables[slot][index] = FindShieldFlashColour(
            g_abShieldFlashQuads,
            red + (g_afShieldFlashColour[0] - red) * alpha,
            green + (g_afShieldFlashColour[1] - green) * alpha,
            blue + (g_afShieldFlashColour[2] - blue) * alpha);
    }
    g_abShieldFlashTableValid[slot] = 1;
    return g_aabShieldFlashTables[slot];
}

void SdlShieldFlashDraw(Viewport *viewport, short obj, float centerX,
                        float centerY)
{
    ShieldFlash *flash;
    const HullField *field;
    RasterSurface surface;
    RasterClip clip;
    FixedVector world;
    float direction[3];
    float x;
    float y;
    float z;
    float distance;
    float radius;
    float projected;
    float fade;
    float length;
    int viewLeft;
    int viewTop;
    int viewWidth;
    int viewHeight;
    int firstX;
    int lastX;
    int firstY;
    int lastY;
    int row;
    int column;
    Uint32 age;

    if (!g_bShieldFlashEnabled || viewport == NULL ||
        viewport->pixels == NULL || obj < 0 || obj >= SPACE_OBJECT_COUNT)
        return;
    flash = &g_aShieldFlashes[obj];
    if (!flash->active)
        return;
    age = SDL_GetTicks() - flash->startTicks;
    if (age >= SHIELD_FLASH_MS) {
        flash->active = 0;
        return;
    }
    fade = 1.0f - (float)age / SHIELD_FLASH_MS;

    /* Impact direction in the camera's frame: ship axes -> world -> eye. */
    world.x = (int)(65536.0f * (flash->local[0] * aShipRightVector[obj].x +
                                flash->local[1] * aShipUpVector[obj].x +
                                flash->local[2] * aShipFwdVec[obj].x) /
                    256.0f);
    world.y = (int)(65536.0f * (flash->local[0] * aShipRightVector[obj].y +
                                flash->local[1] * aShipUpVector[obj].y +
                                flash->local[2] * aShipFwdVec[obj].y) /
                    256.0f);
    world.z = (int)(65536.0f * (flash->local[0] * aShipRightVector[obj].z +
                                flash->local[1] * aShipUpVector[obj].z +
                                flash->local[2] * aShipFwdVec[obj].z) /
                    256.0f);
    direction[0] = DotAxis(&world, &aShipRightVector[EYE_OBJECT]);
    direction[1] = DotAxis(&world, &aShipUpVector[EYE_OBJECT]);
    direction[2] = DotAxis(&world, &aShipFwdVec[EYE_OBJECT]);
    length = SDL_sqrtf(direction[0] * direction[0] +
                       direction[1] * direction[1] +
                       direction[2] * direction[2]);
    if (length <= 0.0f)
        return;
    direction[0] /= length;
    direction[1] /= length;
    direction[2] /= length;

    /* The collision sphere is much tighter than the ship, so use the
     * bounding sphere of the ship3d point cloud when there is one: a model
     * unit is scale/256 world units. */
    radius = (float)asObjectCollisionRadius[obj] * 256.0f;
    field = SdlGetHullField(aeObjectTypeList[obj]);
    if (field != NULL) {
        float hullRadius = SdlGetHullFieldRadius(field) *
                           (float)(unsigned short)awObjectScales[obj];

        if (hullRadius > radius)
            radius = hullRadius;
    }
    x = (float)aObjectViewPositionsList[obj].x;
    y = (float)aObjectViewPositionsList[obj].y;
    z = (float)aObjectViewPositionsList[obj].z;
    distance = SDL_sqrtf(x * x + y * y + z * z);
    if (radius <= 0.0f || distance <= radius || z <= 0.0f)
        return;
    projected = (float)(nScreenWidth & ~1) * 0.5f * radius /
                SDL_sqrtf(distance * distance - radius * radius);
    if (projected < 2.0f)
        projected = 2.0f;

    viewLeft = viewport->left;
    viewTop = viewport->top;
    viewWidth = viewport->right - viewport->left + 1;
    viewHeight = viewport->bottom - viewport->top + 1;
    firstX = (int)SDL_floorf(centerX - projected) - viewLeft;
    lastX = (int)SDL_ceilf(centerX + projected) - viewLeft;
    firstY = (int)SDL_floorf(centerY - projected) - viewTop;
    lastY = (int)SDL_ceilf(centerY + projected) - viewTop;
    if (firstX < 0)
        firstX = 0;
    if (firstY < 0)
        firstY = 0;
    if (lastX >= viewWidth)
        lastX = viewWidth - 1;
    if (lastY >= viewHeight)
        lastY = viewHeight - 1;
    if (lastX < firstX || lastY < firstY)
        return;

    ValidateViewportBounds(viewport, &surface, &clip);
    for (row = firstY; row <= lastY; row++) {
        float dy = ((float)(row + viewTop) + 0.5f - centerY) / projected;

        for (column = firstX; column <= lastX; column++) {
            float dx = ((float)(column + viewLeft) + 0.5f - centerX) /
                       projected;
            float rim2 = dx * dx + dy * dy;
            float facing;
            float alpha;
            int level;
            int current;

            if (rim2 >= 1.0f)
                continue;
            /* Near-side surface point; the camera looks along +z, so the
             * visible hemisphere has negative z. */
            facing = dx * direction[0] + dy * direction[1] -
                     SDL_sqrtf(1.0f - rim2) * direction[2];
            if (facing <= 0.0f)
                continue;
            /* Strongest at the impact point, a little brighter at the
             * silhouette where the shell is seen edge-on. */
            alpha = fade * (0.35f + 0.65f * facing) * (0.75f + 0.25f * rim2);
            level = (int)(alpha * SHIELD_FLASH_LEVELS + 0.5f);
            if (level < 1)
                continue;
            if (level > SHIELD_FLASH_LEVELS)
                level = SHIELD_FLASH_LEVELS;
            current = ReadRasterClipPixel(&clip, column, row) & 0xff;
            SetRasterClipPixel(&clip, column, row,
                               GetShieldFlashTable(level)[current]);
        }
    }
}
