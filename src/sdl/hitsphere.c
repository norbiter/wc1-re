/* Fork-only: --hitspheres draws each ship's collision sphere as a
 * semitransparent magenta disc, to judge how well the game's hit tests
 * (asObjectCollisionRadius) match what is on screen.
 *
 * The space buffer is 8-bit indexed, so the blend is done through a lookup
 * table from every palette index to the palette index nearest its colour
 * mixed with magenta.  The table is rebuilt whenever the palette changes, so
 * palette fades keep working. */
#include "wc1.h"

#include "video_internal.h"

#define HITSPHERE_FILL_ALPHA 0.30f
#define HITSPHERE_RIM_ALPHA 0.85f
#define HITSPHERE_RIM_WIDTH 1.5f

static int g_bHitSphereEnabled;
static unsigned char g_abHitSphereQuads[1024];
static unsigned char g_abHitSphereFill[256];
static unsigned char g_abHitSphereRim[256];
static int g_bHitSphereTablesValid;

void SdlHitSphereEnable(void)
{
    g_bHitSphereEnabled = 1;
}

int SdlHitSphereEnabled(void)
{
    return g_bHitSphereEnabled;
}

static unsigned char FindHitSphereColour(const unsigned char *quads,
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

/* Palette quads hold the 8-bit components the display shows. */
static void UpdateHitSphereTables(void)
{
    const unsigned char *quads = SdlGetPaletteQuads();
    int index;

    if (g_bHitSphereTablesValid &&
        SDL_memcmp(quads, g_abHitSphereQuads, sizeof(g_abHitSphereQuads)) == 0)
        return;
    SDL_memcpy(g_abHitSphereQuads, quads, sizeof(g_abHitSphereQuads));
    g_bHitSphereTablesValid = 1;
    for (index = 0; index < 256; index++) {
        float red = quads[index * 4 + 2];
        float green = quads[index * 4 + 1];
        float blue = quads[index * 4];

        g_abHitSphereFill[index] = FindHitSphereColour(
            g_abHitSphereQuads,
            red + (255.0f - red) * HITSPHERE_FILL_ALPHA,
            green * (1.0f - HITSPHERE_FILL_ALPHA),
            blue + (255.0f - blue) * HITSPHERE_FILL_ALPHA);
        g_abHitSphereRim[index] = FindHitSphereColour(
            g_abHitSphereQuads,
            red + (255.0f - red) * HITSPHERE_RIM_ALPHA,
            green * (1.0f - HITSPHERE_RIM_ALPHA),
            blue + (255.0f - blue) * HITSPHERE_RIM_ALPHA);
    }
}

void SdlHitSphereDraw(Viewport *viewport, short obj, float centerX,
                      float centerY)
{
    RasterSurface surface;
    RasterClip clip;
    float x;
    float y;
    float z;
    float distance;
    float radius;
    float focal;
    float projected;
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

    if (!g_bHitSphereEnabled || viewport == NULL || viewport->pixels == NULL)
        return;
    if (aeObjectClass[obj] != OBJECT_CLASS_CAPITAL_SHIP &&
        aeObjectClass[obj] != OBJECT_CLASS_SHIP)
        return;
    /* The same sphere the collision and weapon tests use, in 1/256 units. */
    radius = (float)asObjectCollisionRadius[obj] * 256.0f;
    x = (float)aObjectViewPositionsList[obj].x;
    y = (float)aObjectViewPositionsList[obj].y;
    z = (float)aObjectViewPositionsList[obj].z;
    distance = SDL_sqrtf(x * x + y * y + z * z);
    if (radius <= 0.0f || distance <= radius || z <= 0.0f)
        return;
    focal = (float)(nScreenWidth & ~1) * 0.5f;
    /* Angular radius of the sphere seen from the eye. */
    projected = focal * radius / SDL_sqrtf(distance * distance - radius * radius);
    if (projected < 1.0f)
        projected = 1.0f;

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

    UpdateHitSphereTables();
    ValidateViewportBounds(viewport, &surface, &clip);
    for (row = firstY; row <= lastY; row++) {
        float dy = (float)(row + viewTop) + 0.5f - centerY;

        for (column = firstX; column <= lastX; column++) {
            float dx = (float)(column + viewLeft) + 0.5f - centerX;
            float edge = projected - SDL_sqrtf(dx * dx + dy * dy);
            int current;

            if (edge < 0.0f)
                continue;
            current = ReadRasterClipPixel(&clip, column, row) & 0xff;
            SetRasterClipPixel(&clip, column, row,
                               edge < HITSPHERE_RIM_WIDTH
                                   ? g_abHitSphereRim[current]
                                   : g_abHitSphereFill[current]);
        }
    }
}
