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

/* The volume a laser bolt's centre cannot enter without hitting: the hull
 * inflated by the bolt's radius.  Each pixel's ray runs along the camera's
 * z axis through the hull's signed distance field and stops where the field
 * drops below the bolt radius.  Returns 0 when the ship has no hull field. */
static int DrawHullVolume(Viewport *viewport, short obj, float centerX,
                          float centerY)
{
    const HullField *field;
    float basis[3][3];
    RasterSurface surface;
    RasterClip clip;
    float unitsPerModel; /* world units per model unit */
    float depth;
    float pixelsPerModel;
    float boltRadius;    /* model units */
    float bound;         /* model units, bounding radius incl. bolt */
    float cell;
    float extent;
    int viewLeft = viewport->left;
    int viewTop = viewport->top;
    int viewWidth = viewport->right - viewport->left + 1;
    int viewHeight = viewport->bottom - viewport->top + 1;
    int firstX;
    int lastX;
    int firstY;
    int lastY;
    int row;
    int column;

    field = SdlGetHullField(aeObjectTypeList[obj]);
    if (field == NULL)
        return 0;
    depth = (float)aObjectViewPositionsList[obj].z / 256.0f;
    if (depth <= 0.0f)
        return 1;
    /* The engine sizes a sprite as scale/256 pixels per model unit when it
     * is far away, so a model unit is scale/256 world units. */
    unitsPerModel = (float)(unsigned short)awObjectScales[obj] / 256.0f;
    if (unitsPerModel <= 0.0f)
        return 1;
    pixelsPerModel = (float)(nScreenWidth & ~1) * 0.5f * unitsPerModel / depth;
    boltRadius = (float)aObjectTypeData[OBJECT_TYPE_LASER_CANNON]
                     .collisionRadius / unitsPerModel;
    cell = SdlGetHullFieldCell(field);
    bound = SdlGetHullFieldRadius(field) + boltRadius;
    extent = bound * pixelsPerModel;
    firstX = (int)SDL_floorf(centerX - extent) - viewLeft;
    lastX = (int)SDL_ceilf(centerX + extent) - viewLeft;
    firstY = (int)SDL_floorf(centerY - extent) - viewTop;
    lastY = (int)SDL_ceilf(centerY + extent) - viewTop;
    if (firstX < 0)
        firstX = 0;
    if (firstY < 0)
        firstY = 0;
    if (lastX >= viewWidth)
        lastX = viewWidth - 1;
    if (lastY >= viewHeight)
        lastY = viewHeight - 1;
    if (lastX < firstX || lastY < firstY)
        return 1;

    SdlShip3dComputeBasis(obj, basis);
    UpdateHitSphereTables();
    ValidateViewportBounds(viewport, &surface, &clip);
    for (row = firstY; row <= lastY; row++) {
        float cameraY = ((float)(row + viewTop) + 0.5f - centerY) /
                        pixelsPerModel;

        for (column = firstX; column <= lastX; column++) {
            float cameraX = ((float)(column + viewLeft) + 0.5f - centerX) /
                            pixelsPerModel;
            float start[3];
            float direction[3];
            float travel = 0.0f;
            int axis;
            int iteration;
            int hit = 0;

            if (cameraX * cameraX + cameraY * cameraY > bound * bound)
                continue;
            /* Camera -> model is the transpose of the model -> camera
             * rotation; the ray starts at the near side of the bound. */
            for (axis = 0; axis < 3; axis++) {
                start[axis] = basis[0][axis] * cameraX +
                              basis[1][axis] * cameraY -
                              basis[2][axis] * bound;
                direction[axis] = basis[2][axis];
            }
            for (iteration = 0; iteration < 160 && travel < 2.0f * bound;
                 iteration++) {
                float d = SdlSampleHullField(
                    field, start[0] + direction[0] * travel,
                    start[1] + direction[1] * travel,
                    start[2] + direction[2] * travel);

                if (d <= boltRadius) {
                    hit = 1;
                    break;
                }
                travel += d - boltRadius > 0.25f * cell
                              ? d - boltRadius : 0.25f * cell;
            }
            if (hit) {
                int current = ReadRasterClipPixel(&clip, column, row) & 0xff;

                SetRasterClipPixel(&clip, column, row,
                                   g_abHitSphereFill[current]);
            }
        }
    }
    return 1;
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
    if (DrawHullVolume(viewport, obj, centerX, centerY))
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
