/* Fork-only: --dust replaces the eight space-dust sprites with a field of
 * fine motes.
 *
 * The original keeps dust in eight slots of the object table and re-seeds
 * one ahead of the ship whenever one leaves the screen.  They are sprites of
 * a pixel or more, they move once per sim tick, and the same slots are lent
 * to asteroids, mines and anything else that needs an object.
 *
 * This field is separate from the object table and purely visual.  The motes
 * stand still in space on a lattice that repeats every DUST_CELL in each
 * direction, and the cube of it centred on the eye is drawn.  A mote's place
 * on screen depends on nothing but the eye's position and frame, so there is
 * nothing to spawn or advance, the sim's random sequence is untouched, and
 * the in-between frames of --smooth move the dust simply by moving the eye.
 * Motes fade out towards the edge of the cube, so the repeat is never seen.
 *
 * Motion shows in two ways.  Parallax: near motes cross the view faster than
 * far ones.  And each mote is drawn as the path it covered over the last
 * DUST_EXPOSURE_TICKS of the eye's own travel, fading towards the tail, so
 * the field stretches into streaks that point away from the heading as the
 * ship gathers speed and shrinks back to dots as it stops.  Turning does not
 * streak them.
 *
 * The original motes cycle through four sprite frames as if tumbling.  Here
 * each mote's brightness swings gently at a rate and phase of its own, with
 * a brief glint once per turn, as a facet catches the light.  This runs on
 * the wall clock, not the sim tick, so it is as smooth as the frame rate.
 *
 * With --enhanced the motes go to the GL object layer as translucent quads
 * at output resolution, a single display pixel wide at their smallest.
 * Otherwise they are plotted into the 8-bit space buffer as single pixels,
 * through a short ramp from the background to the dust colour.  Either way
 * they are drawn before the objects, and so behind them.
 *
 * The colour is the brightest one in the game's own dust sprites, looked up
 * in the current palette every frame, so it follows palette fades.
 *
 * The original motes keep their object slots and are only not drawn, so
 * borrow_dust, hazard fields and explosion sparks work as before. */
#include "wc1.h"

#include "video_internal.h"

#define DUST_MOTE_COUNT 2048
/* The lattice period, in the 1/256 world units positions are kept in: 4096
 * world units.  A power of two, so that wrapping is a mask. */
#define DUST_CELL_BITS 20
#define DUST_CELL (1U << DUST_CELL_BITS)
#define DUST_CELL_MASK (DUST_CELL - 1U)
#define DUST_RANGE ((float)(DUST_CELL / 2U))
/* Motes are fully visible out to this fraction of the range. */
#define DUST_FADE_START 0.45f
#define DUST_NEAR (20.0f * 256.0f)
/* A mote's radius in the world, and the limits of its width on screen in
 * 320x200 pixels. */
#define DUST_MOTE_RADIUS (0.45f * 256.0f)
#define DUST_MAX_WIDTH 0.75f
#define DUST_MIN_WIDTH 0.12f
#define DUST_PEAK_ALPHA 0.85f
/* A mote turns once in 0.4 to 1.6 seconds.  Its brightness swings by
 * DUST_SHIMMER either way and rises by up to DUST_GLINT in the glint. */
#define DUST_MIN_TURN_HZ 0.6f
#define DUST_MAX_TURN_HZ 2.5f
#define DUST_SHIMMER 0.22f
#define DUST_GLINT 0.55f
#define DUST_EXPOSURE_TICKS 1.0f
#define DUST_MAX_STREAK 48.0f
#define DUST_LEVELS 8
#define DUST_SPRITE_FRAMES 32
#define DUST_SPRITE_PIXELS 4096

typedef struct DustMote {
    Uint32 x;
    Uint32 y;
    Uint32 z;
    float brightness;
    float turnRate;
    float turnPhase;
} DustMote;

static int g_bDustEnabled;
static int g_bDustSeeded;
static DustMote g_aDustMotes[DUST_MOTE_COUNT];
static float g_afDustVertices[DUST_MOTE_COUNT * 6 * 6];
static struct Shape *g_pDustColourShape;
static int g_nDustColourIndex = -1;
static unsigned char g_abDustQuads[1024];
static unsigned char g_abDustRamp[DUST_LEVELS + 1];
static int g_nDustRampClear = -1;
static int g_nDustRampColour = -1;

void SdlDustEnable(void)
{
    g_bDustEnabled = 1;
}

static int DustActive(void)
{
    return g_bDustEnabled && !SdlSwcFlightActive();
}

/* Nonzero for an original dust mote the field stands in for.  Explosion
 * sparks share the class but not the type. */
int SdlDustHidesObject(short obj)
{
    return DustActive() && aeObjectClass[obj] == OBJECT_CLASS_DUST &&
           aeObjectTypeList[obj] == OBJECT_TYPE_SPACE_DUST;
}

/* A generator of its own: the game's would change what the sim rolls. */
static void SeedDust(void)
{
    Uint32 state = 0x9e3779b9U;
    Uint32 values[6];
    int index;
    int part;

    for (index = 0; index < DUST_MOTE_COUNT; index++) {
        for (part = 0; part < 6; part++) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            values[part] = state >> 8;
        }
        g_aDustMotes[index].x = values[0] & DUST_CELL_MASK;
        g_aDustMotes[index].y = values[1] & DUST_CELL_MASK;
        g_aDustMotes[index].z = values[2] & DUST_CELL_MASK;
        g_aDustMotes[index].brightness =
            0.45f + 0.55f * (float)(values[3] & 0xffU) / 255.0f;
        g_aDustMotes[index].turnRate =
            DUST_MIN_TURN_HZ + (DUST_MAX_TURN_HZ - DUST_MIN_TURN_HZ) *
                                   (float)(values[4] & 0xffffU) / 65535.0f;
        g_aDustMotes[index].turnPhase =
            (float)(values[5] & 0xffffU) / 65536.0f;
    }
    g_bDustSeeded = 1;
}

static int FindNearestColour(const unsigned char *quads, float red,
                             float green, float blue)
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
    return best;
}

/* The palette entry of the brightest pixel in the game's dust sprites, the
 * first 32 frames of the constellation shape. */
static int GetDustColourIndex(const unsigned char *quads)
{
    static unsigned char pixels[DUST_SPRITE_PIXELS];
    struct Shape *shape = pCurrentConstellationShape;
    int best = -1;
    int bestLuma = -1;
    short frames;
    short frame;

    if (shape == g_pDustColourShape && g_nDustColourIndex >= 0)
        return g_nDustColourIndex;
    if (shape != 0) {
        frames = GetShapeFrameCount(shape);
        if (frames > DUST_SPRITE_FRAMES)
            frames = DUST_SPRITE_FRAMES;
        for (frame = 0; frame < frames; frame++) {
            short width = 0;
            short height = 0;
            short leftExtent = 0;
            short topExtent = 0;
            int count;
            int index;

            GetShapeFrameExtents(shape, frame, &width, &height, &leftExtent,
                                 &topExtent);
            if (width <= 0 || height <= 0 ||
                (int)width * height > DUST_SPRITE_PIXELS)
                continue;
            count = (int)width * height;
            SDL_memset(pixels, 0xff, (size_t)count);
            DecodeShapeFrame(shape, frame, pixels, width, height, leftExtent,
                             topExtent);
            for (index = 0; index < count; index++) {
                int entry = pixels[index];
                int luma;

                if (entry == 0xff)
                    continue;
                luma = quads[entry * 4 + 2] * 2 + quads[entry * 4 + 1] * 5 +
                       quads[entry * 4];
                if (luma > bestLuma) {
                    bestLuma = luma;
                    best = entry;
                }
            }
        }
    }
    if (best < 0 || bestLuma == 0) {
        /* No sprite to go by, or a palette faded to black: decide again
         * next frame. */
        g_nDustColourIndex = -1;
        return FindNearestColour(quads, 200.0f, 200.0f, 210.0f);
    }
    g_pDustColourShape = shape;
    g_nDustColourIndex = best;
    return best;
}

/* The 8-bit ramp from the background colour to the dust colour. */
static const unsigned char *GetDustRamp(const unsigned char *quads,
                                        int clearIndex, int colourIndex)
{
    int level;

    if (g_nDustRampClear == clearIndex && g_nDustRampColour == colourIndex &&
        SDL_memcmp(quads, g_abDustQuads, sizeof(g_abDustQuads)) == 0)
        return g_abDustRamp;
    SDL_memcpy(g_abDustQuads, quads, sizeof(g_abDustQuads));
    g_nDustRampClear = clearIndex;
    g_nDustRampColour = colourIndex;
    for (level = 0; level <= DUST_LEVELS; level++) {
        float mix = (float)level / DUST_LEVELS;
        float red = quads[clearIndex * 4 + 2];
        float green = quads[clearIndex * 4 + 1];
        float blue = quads[clearIndex * 4];

        g_abDustRamp[level] = (unsigned char)FindNearestColour(
            quads, red + (quads[colourIndex * 4 + 2] - red) * mix,
            green + (quads[colourIndex * 4 + 1] - green) * mix,
            blue + (quads[colourIndex * 4] - blue) * mix);
    }
    g_abDustRamp[0] = (unsigned char)clearIndex;
    g_abDustRamp[DUST_LEVELS] = (unsigned char)colourIndex;
    return g_abDustRamp;
}

static void PlotDustPixel(Viewport *viewport, const unsigned char *ramp,
                          float x, float y, float alpha)
{
    int column = (int)SDL_floorf(x + 0.5f);
    int row = (int)SDL_floorf(y + 0.5f);
    int level = (int)(alpha * DUST_LEVELS + 0.5f);
    unsigned char *pixel;

    if (level < 1 || column < viewport->left || column > viewport->right ||
        row < viewport->top || row > viewport->bottom)
        return;
    if (level > DUST_LEVELS)
        level = DUST_LEVELS;
    pixel = viewport->pixels + viewport->rowOffsets[row] + column;
    /* Keep the brighter of two motes that share a pixel. */
    if (*pixel == ramp[0] || level == DUST_LEVELS)
        *pixel = ramp[level];
}

/* A mote in the 8-bit buffer: one pixel, trailed by dimmer ones along the
 * streak. */
static void DrawDustPixels(Viewport *viewport, const unsigned char *ramp,
                           float headX, float headY, float tailX, float tailY,
                           float length, float alpha)
{
    int steps = (int)length;
    int step;

    for (step = steps; step >= 1; step--) {
        float along = (float)step / (length > 1.0f ? length : 1.0f);

        PlotDustPixel(viewport, ramp, headX + (tailX - headX) * along,
                      headY + (tailY - headY) * along,
                      alpha * (1.0f - along));
    }
    PlotDustPixel(viewport, ramp, headX, headY, alpha);
}

/* A mote in the GL layer: a quad round the streak, as two triangles, opaque
 * at the head and clear at the tail. */
static float *AppendDustQuad(float *out, float headX, float headY,
                             float tailX, float tailY, float length,
                             float width, float alpha)
{
    static const int end[6] = { 0, 0, 1, 0, 1, 1 };
    static const int side[6] = { 0, 1, 1, 0, 1, 0 };
    float half = width * 0.5f;
    float alongX = 1.0f;
    float alongY = 0.0f;
    int corner;

    if (length > 0.001f) {
        alongX = (tailX - headX) / length;
        alongY = (tailY - headY) / length;
    }
    for (corner = 0; corner < 6; corner++) {
        float reach = end[corner] ? length + half : -half;
        float across = side[corner] ? half : -half;

        out[0] = headX + alongX * reach - alongY * across;
        out[1] = headY + alongY * reach + alongX * across;
        out[2] = 0.0f;
        out[3] = 0.0f;
        out[4] = 0.0f;
        out[5] = end[corner] && length > half ? 0.0f : alpha;
        out += 6;
    }
    return out;
}

static float DotFixed(const float v[3], const FixedVector *axis)
{
    return v[0] * (float)axis->x + v[1] * (float)axis->y +
           v[2] * (float)axis->z;
}

/* Draws the field for the eye as it stands.  Called once per presented
 * frame, after the space frame is opened and before the objects. */
void SdlDustDraw(Viewport *viewport)
{
    const unsigned char *quads;
    const unsigned char *ramp = 0;
    const FixedVector *right = &aShipRightVector[EYE_OBJECT];
    const FixedVector *up = &aShipUpVector[EYE_OBJECT];
    const FixedVector *forward = &aShipFwdVec[EYE_OBJECT];
    float *out = g_afDustVertices;
    float colour[3];
    float drift[3];
    float world[3];
    float axisLength;
    float focal;
    float centreX;
    float centreY;
    float minWidth;
    float left;
    float top;
    float rightEdge;
    float bottom;
    double seconds;
    Uint32 eyeX;
    Uint32 eyeY;
    Uint32 eyeZ;
    int colourIndex;
    int useGl;
    int index;

    if (!DustActive() || viewport == 0 || viewport->pixels == 0 ||
        viewport->rowOffsets == 0)
        return;
    world[0] = (float)forward->x;
    world[1] = (float)forward->y;
    world[2] = (float)forward->z;
    axisLength = SDL_sqrtf(world[0] * world[0] + world[1] * world[1] +
                           world[2] * world[2]);
    if (axisLength <= 0.0f)
        return;
    if (!g_bDustSeeded)
        SeedDust();
    quads = SdlGetPaletteQuads();
    colourIndex = GetDustColourIndex(quads);
    useGl = SdlUsingGlRenderer() && !SdlEgaDitherEnabled();
    minWidth = DUST_MIN_WIDTH;
    if (useGl) {
        float scale = SdlGlRendererOutputScale();

        colour[0] = quads[colourIndex * 4 + 2] / 255.0f;
        colour[1] = quads[colourIndex * 4 + 1] / 255.0f;
        colour[2] = quads[colourIndex * 4] / 255.0f;
        /* Never thinner than one display pixel, or motes would drop out
         * between pixel centres. */
        if (scale > 0.0f && 1.0f / scale > minWidth)
            minWidth = 1.0f / scale;
    } else {
        ramp = GetDustRamp(quads, cPrimaryViewBufferColour & 0xff,
                           colourIndex);
    }

    /* What the eye covered during the exposure, in its own frame: relative
     * to the eye, every mote came from that much further along it. */
    world[0] = (float)aShipVelocity[EYE_OBJECT].x * DUST_EXPOSURE_TICKS;
    world[1] = (float)aShipVelocity[EYE_OBJECT].y * DUST_EXPOSURE_TICKS;
    world[2] = (float)aShipVelocity[EYE_OBJECT].z * DUST_EXPOSURE_TICKS;
    drift[0] = DotFixed(world, right) / axisLength;
    drift[1] = DotFixed(world, up) / axisLength;
    drift[2] = DotFixed(world, forward) / axisLength;

    seconds = (double)SDL_GetTicks() / 1000.0;
    focal = SdlFovFocal();
    centreX = (float)nCurrentViewCenterPosX;
    centreY = (float)nCurrentViewCenterPosY;
    left = (float)viewport->left - DUST_MAX_STREAK;
    top = (float)viewport->top - DUST_MAX_STREAK;
    rightEdge = (float)viewport->right + DUST_MAX_STREAK;
    bottom = (float)viewport->bottom + DUST_MAX_STREAK;
    eyeX = (Uint32)aShipPositionsArray[EYE_OBJECT].x;
    eyeY = (Uint32)aShipPositionsArray[EYE_OBJECT].y;
    eyeZ = (Uint32)aShipPositionsArray[EYE_OBJECT].z;

    for (index = 0; index < DUST_MOTE_COUNT; index++) {
        const DustMote *mote = &g_aDustMotes[index];
        float view[3];
        float tail[3];
        float distance;
        float headX;
        float headY;
        float tailX;
        float tailY;
        float length;
        float natural;
        float width;
        float alpha;
        double turn;
        float swing;
        float glint;

        /* The copy of the mote nearest the eye. */
        world[0] = (float)(int)((mote->x - eyeX + DUST_CELL / 2U) &
                                DUST_CELL_MASK) - DUST_RANGE;
        world[1] = (float)(int)((mote->y - eyeY + DUST_CELL / 2U) &
                                DUST_CELL_MASK) - DUST_RANGE;
        world[2] = (float)(int)((mote->z - eyeZ + DUST_CELL / 2U) &
                                DUST_CELL_MASK) - DUST_RANGE;
        view[2] = DotFixed(world, forward) / axisLength;
        if (view[2] < DUST_NEAR)
            continue;
        distance = SDL_sqrtf(world[0] * world[0] + world[1] * world[1] +
                             world[2] * world[2]);
        if (distance >= DUST_RANGE)
            continue;
        view[0] = DotFixed(world, right) / axisLength;
        view[1] = DotFixed(world, up) / axisLength;
        headX = centreX + focal * view[0] / view[2];
        headY = centreY + focal * view[1] / view[2];
        if (headX < left || headX > rightEdge || headY < top ||
            headY > bottom)
            continue;

        tail[0] = view[0] + drift[0];
        tail[1] = view[1] + drift[1];
        tail[2] = view[2] + drift[2];
        if (tail[2] < DUST_NEAR) {
            /* Flying backwards: cut the streak where it meets the near
             * plane. */
            float keep = (view[2] - DUST_NEAR) / (view[2] - tail[2]);

            tail[0] = view[0] + drift[0] * keep;
            tail[1] = view[1] + drift[1] * keep;
            tail[2] = DUST_NEAR;
        }
        tailX = centreX + focal * tail[0] / tail[2];
        tailY = centreY + focal * tail[1] / tail[2];
        length = SDL_sqrtf((tailX - headX) * (tailX - headX) +
                           (tailY - headY) * (tailY - headY));
        if (length > DUST_MAX_STREAK) {
            tailX = headX + (tailX - headX) * DUST_MAX_STREAK / length;
            tailY = headY + (tailY - headY) * DUST_MAX_STREAK / length;
            length = DUST_MAX_STREAK;
        }

        turn = seconds * mote->turnRate + mote->turnPhase;
        swing = SDL_sinf(6.28318531f * (float)(turn - SDL_floor(turn)));
        glint = swing > 0.0f ? swing * swing : 0.0f;
        glint *= glint;
        glint *= glint;
        alpha = DUST_PEAK_ALPHA * mote->brightness *
                (1.0f + DUST_SHIMMER * swing + DUST_GLINT * glint);
        if (alpha > 1.0f)
            alpha = 1.0f;
        if (distance > DUST_RANGE * DUST_FADE_START)
            alpha *= (DUST_RANGE - distance) /
                     (DUST_RANGE * (1.0f - DUST_FADE_START));
        if (view[2] < DUST_NEAR * 2.0f)
            alpha *= (view[2] - DUST_NEAR) / DUST_NEAR;
        natural = focal * DUST_MOTE_RADIUS * 2.0f / view[2];
        width = natural;
        if (width > DUST_MAX_WIDTH)
            width = DUST_MAX_WIDTH;
        if (width < minWidth) {
            /* Too small to draw at its own size: draw the smallest mote
             * there is, dimmer. */
            alpha *= 0.35f + 0.65f * natural / minWidth;
            width = minWidth;
        }
        if (useGl) {
            /* A streak spreads the mote's light along its length. */
            alpha /= SDL_sqrtf(1.0f + length / (width * 6.0f));
            out = AppendDustQuad(out, headX, headY, tailX, tailY, length,
                                 width, alpha);
        } else {
            alpha /= SDL_sqrtf(1.0f + length / 6.0f);
            DrawDustPixels(viewport, ramp, headX, headY, tailX, tailY,
                           length, alpha);
        }
    }
    if (useGl && out != g_afDustVertices &&
        !SdlGlRendererRecordShipOverlay(
            viewport, g_afDustVertices,
            (int)((out - g_afDustVertices) / 6), colour)) {
        /* The layer is not recording this frame (a scene outside flight):
         * the frame is composed in the 8-bit buffer, so the dust is too. */
        ramp = GetDustRamp(quads, cPrimaryViewBufferColour & 0xff,
                           colourIndex);
        for (index = 0; index < (int)((out - g_afDustVertices) / 36);
             index++) {
            const float *quad = g_afDustVertices + index * 36;

            PlotDustPixel(viewport, ramp, (quad[0] + quad[6]) * 0.5f,
                          (quad[1] + quad[7]) * 0.5f, quad[5]);
        }
    }
}
