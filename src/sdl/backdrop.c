/* Fork-only: --fine-backdrop redraws the backdrop's planets, suns, nebulae,
 * asteroids and comets at output resolution.
 *
 * Each backdrop object is a sprite of 16 to 68 game pixels, enlarged five to
 * ten times on a modern display.  How best to enlarge one depends on what it
 * shows, so each shape id gets one of four treatments:
 *
 *   light   Suns with their painted corona, nebulae, the galaxy and the
 *           comets.  The sprite is light on black, so its brightness becomes
 *           its opacity: the glow fades into the backdrop and the stars show
 *           through its thin parts.
 *   clean   Asteroids.  Colour is smoothed out of its dither, and the
 *           outline is redrawn from a distance field of the sprite's mask,
 *           a display pixel wide.
 *   sphere  Planets.  The limb of each body is fitted (a circle, allowed to
 *           be up to a third wider than tall, as some were drawn), drawn
 *           exactly, and the sprite's smoothed colours are laid on it.  Where
 *           the painting stops short of the limb, the rest is night side; a
 *           thin atmosphere glows on the lit side.
 *
 *   ringed  The ringed planet, redrawn as a scene: a banded sphere and a
 *           tilted flat ring around it, placed to match the sprite.  The
 *           ring's near half crosses in front of the planet, and the
 *           planet's shadow darkens its far half.
 *
 * The images are built on the CPU when an object is first drawn at a given
 * size, and drawn by the GL layer in the object's place in the painter
 * order.  They are built from the palette of that moment, and a tint keeps
 * them in step with palette fades afterwards.  Without --enhanced, or when
 * a shape cannot be treated (a planet no circle fits), the sprite is drawn
 * as before. */
#include "wc1.h"

#include <math.h>

#include "video_internal.h"

#define BACKDROP_PAD 3
#define BACKDROP_MAX_SIDE 96
#define BACKDROP_MAX_CELLS (BACKDROP_MAX_SIDE * BACKDROP_MAX_SIDE)
#define BACKDROP_MAX_SCALE 10
#define BACKDROP_MAX_BODIES 4
#define BACKDROP_CACHE_CAPACITY 8
#define BACKDROP_SHAPE_COUNT 28
#define BACKDROP_TRANSPARENT 0xff

enum {
    LOOK_SPRITE,
    LOOK_LIGHT,
    LOOK_CLEAN,
    LOOK_SPHERE,
    LOOK_RINGED
};

/* By shape id, as the campaign tables name them: ids 0-10 are PLANETS.VGA
 * packets 1-11.  Ids 12-27 appear only in Secret Missions 2; they match
 * PLANETS1.VGA's sixteen packets in order. */
static const unsigned char g_abBackdropLook[BACKDROP_SHAPE_COUNT] = {
    LOOK_LIGHT,  /* 0: violet nebula */
    LOOK_LIGHT,  /* 1: red nebula */
    LOOK_LIGHT,  /* 2: spiral galaxy */
    LOOK_SPHERE, /* 3: blue planet, partly lit */
    LOOK_LIGHT,  /* 4: white star */
    LOOK_SPHERE, /* 5: banded giant, half lit */
    LOOK_LIGHT,  /* 6: red sun */
    LOOK_SPHERE, /* 7: red planet */
    LOOK_RINGED, /* 8: ringed planet */
    LOOK_LIGHT,  /* 9: yellow sun */
    LOOK_LIGHT,  /* 10: red giant */
    LOOK_SPRITE, /* 11: unused */
    LOOK_SPHERE, /* 12: blue planet with two moons */
    LOOK_SPHERE, /* 13: small planet and moon */
    LOOK_LIGHT,  /* 14: large yellow sun */
    LOOK_LIGHT,  /* 15: small yellow sun */
    LOOK_SPHERE, /* 16: orange planet */
    LOOK_SPHERE, /* 17: small orange planet */
    LOOK_LIGHT,  /* 18: speckled red sun */
    LOOK_SPHERE, /* 19: green planet */
    LOOK_SPHERE, /* 20: small green planet */
    LOOK_LIGHT,  /* 21: blue-white star */
    LOOK_LIGHT,  /* 22: small blue-white star */
    LOOK_CLEAN,  /* 23: blue asteroid */
    LOOK_CLEAN,  /* 24: asteroid pair */
    LOOK_CLEAN,  /* 25: small asteroid pair */
    LOOK_LIGHT,  /* 26: comet */
    LOOK_LIGHT,  /* 27: comet */
};

typedef struct BackdropBody {
    float x;
    float y;
    float radius;
    /* How much wider than tall the limb is. */
    float stretch;
    float lightX;
    float lightY;
    float lightStrength;
    float mean[3];
} BackdropBody;

/* The sprite on a grid with a transparent margin, and what is derived from
 * it.  Colours are 0..1. */
typedef struct BackdropGrid {
    int width;
    int height;
    unsigned char index[BACKDROP_MAX_CELLS];
    float alpha[BACKDROP_MAX_CELLS];
    float colour[3][BACKDROP_MAX_CELLS];
    /* De-dithered colour, spread past the outline. */
    float smooth[3][BACKDROP_MAX_CELLS];
    /* Signed distance to the outline in game pixels, positive inside. */
    float distance[BACKDROP_MAX_CELLS];
    short body[BACKDROP_MAX_CELLS];
    int bodyCount;
    BackdropBody bodies[BACKDROP_MAX_BODIES];
} BackdropGrid;

typedef struct BackdropImage {
    short shapeId;
    int scale;
    unsigned int generation;
    /* 0 with failed set: this shape cannot be treated; draw the sprite. */
    int handle;
    int failed;
    int width;
    int height;
    /* The sprite's own frame: its size and origin. */
    short spriteWidth;
    short spriteHeight;
    short originX;
    short originY;
    /* How many pixels use each palette entry, and the colour they summed to
     * when the image was built. */
    unsigned short entryCount[256];
    float builtColour[3];
    unsigned int lastUse;
} BackdropImage;

static int g_bFineBackdropEnabled;
static short g_asObjectShapeId[SPACE_OBJECT_COUNT];
static struct Shape *g_apObjectShape[SPACE_OBJECT_COUNT];
static BackdropImage g_aBackdropImages[BACKDROP_CACHE_CAPACITY];
static unsigned int g_uBackdropUse;
static BackdropGrid g_stBackdropGrid;
static unsigned char g_abBackdropPixels[BACKDROP_MAX_CELLS];
static int g_aiBackdropScratch[BACKDROP_MAX_CELLS];
static float g_afBackdropScratch[BACKDROP_MAX_CELLS];
static short g_asBackdropEdgeIn[BACKDROP_MAX_CELLS * 2];
static short g_asBackdropEdgeOut[BACKDROP_MAX_CELLS * 2];

static const float g_afBlurKernel[9] = { 1, 2, 1, 2, 4, 2, 1, 2, 1 };

void SdlFineBackdropEnable(void)
{
    g_bFineBackdropEnabled = 1;
}

int SdlFineBackdropEnabled(void)
{
    return g_bFineBackdropEnabled;
}

/* InitializeConstellationObject reports which shape id each backdrop object
 * took, since the loaded packet does not say. */
void SdlFineBackdropNoteShape(short obj, short shapeId)
{
    if (obj < 0 || obj >= SPACE_OBJECT_COUNT)
        return;
    g_asObjectShapeId[obj] = shapeId;
    g_apObjectShape[obj] = apObjShapeArray[obj];
}

static float Clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float SmoothStep(float edge0, float edge1, float value)
{
    float t = Clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

static void CatmullRomWeights(float t, float weights[4])
{
    float t2 = t * t;
    float t3 = t2 * t;

    weights[0] = (-t3 + 2.0f * t2 - t) * 0.5f;
    weights[1] = (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f;
    weights[2] = (-3.0f * t3 + 4.0f * t2 + t) * 0.5f;
    weights[3] = (t3 - t2) * 0.5f;
}

/* Samples a channel of the grid at (x, y), cell centres at whole numbers. */
static float SampleBicubic(const float *channel, int width, int height,
                           float x, float y)
{
    float weightX[4];
    float weightY[4];
    float value = 0.0f;
    int baseX = (int)floorf(x);
    int baseY = (int)floorf(y);
    int row;
    int column;

    CatmullRomWeights(x - (float)baseX, weightX);
    CatmullRomWeights(y - (float)baseY, weightY);
    for (row = 0; row < 4; row++) {
        int cellY = baseY - 1 + row;
        float sum = 0.0f;

        cellY = cellY < 0 ? 0 : cellY >= height ? height - 1 : cellY;
        for (column = 0; column < 4; column++) {
            int cellX = baseX - 1 + column;

            cellX = cellX < 0 ? 0 : cellX >= width ? width - 1 : cellX;
            sum += weightX[column] * channel[cellY * width + cellX];
        }
        value += weightY[row] * sum;
    }
    return value;
}

/* Blurs the opaque cells' colours over their opaque neighbours, which
 * dissolves the dither, then spreads colour outward past the outline so
 * that smooth sampling near the edge does not pull in black. */
static void SmoothGridColour(BackdropGrid *grid)
{
    int width = grid->width;
    int height = grid->height;
    int *filled = g_aiBackdropScratch;
    int pass;
    int x;
    int y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int cell = y * width + x;
            float sum[3] = { 0.0f, 0.0f, 0.0f };
            float weight = 0.0f;
            int dx;
            int dy;
            int channel;

            filled[cell] = 0;
            if (grid->alpha[cell] == 0.0f)
                continue;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    int neighbour;
                    float k;

                    if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                        continue;
                    neighbour = ny * width + nx;
                    if (grid->alpha[neighbour] == 0.0f)
                        continue;
                    k = g_afBlurKernel[(dy + 1) * 3 + dx + 1];
                    weight += k;
                    for (channel = 0; channel < 3; channel++)
                        sum[channel] += grid->colour[channel][neighbour] * k;
                }
            }
            for (channel = 0; channel < 3; channel++)
                grid->smooth[channel][cell] = sum[channel] / weight;
            filled[cell] = 1;
        }
    }
    for (pass = 0; pass < BACKDROP_MAX_SIDE; pass++) {
        int changed = 0;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                int cell = y * width + x;
                float sum[3] = { 0.0f, 0.0f, 0.0f };
                int count = 0;
                int dx;
                int dy;
                int channel;

                if (filled[cell] != 0)
                    continue;
                for (dy = -1; dy <= 1; dy++) {
                    for (dx = -1; dx <= 1; dx++) {
                        int nx = x + dx;
                        int ny = y + dy;
                        int neighbour;

                        if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                            continue;
                        neighbour = ny * width + nx;
                        if (filled[neighbour] != 1)
                            continue;
                        count++;
                        for (channel = 0; channel < 3; channel++)
                            sum[channel] += grid->smooth[channel][neighbour];
                    }
                }
                if (count == 0)
                    continue;
                for (channel = 0; channel < 3; channel++)
                    grid->smooth[channel][cell] = sum[channel] / (float)count;
                filled[cell] = 2;
                changed = 1;
            }
        }
        for (x = 0; x < width * height; x++) {
            if (filled[x] == 2)
                filled[x] = 1;
        }
        if (!changed)
            break;
    }
}

/* Lists the cells on each side of the outline, as x, y pairs. */
static void FindGridEdges(const BackdropGrid *grid, int *insideCount,
                          int *outsideCount)
{
    static const int stepX[4] = { 1, -1, 0, 0 };
    static const int stepY[4] = { 0, 0, 1, -1 };
    int x;
    int y;

    *insideCount = 0;
    *outsideCount = 0;
    for (y = 0; y < grid->height; y++) {
        for (x = 0; x < grid->width; x++) {
            float own = grid->alpha[y * grid->width + x];
            int edge = 0;
            int step;

            for (step = 0; step < 4; step++) {
                int nx = x + stepX[step];
                int ny = y + stepY[step];

                if (nx < 0 || ny < 0 || nx >= grid->width ||
                    ny >= grid->height)
                    continue;
                if (grid->alpha[ny * grid->width + nx] != own)
                    edge = 1;
            }
            if (!edge)
                continue;
            if (own != 0.0f) {
                g_asBackdropEdgeIn[*insideCount * 2] = (short)x;
                g_asBackdropEdgeIn[*insideCount * 2 + 1] = (short)y;
                (*insideCount)++;
            } else {
                g_asBackdropEdgeOut[*outsideCount * 2] = (short)x;
                g_asBackdropEdgeOut[*outsideCount * 2 + 1] = (short)y;
                (*outsideCount)++;
            }
        }
    }
}

/* The signed distance from each cell to the outline, then lightly blurred
 * to round off the stair steps. */
static void BuildGridDistance(BackdropGrid *grid, int insideCount,
                              int outsideCount)
{
    float *raw = g_afBackdropScratch;
    int width = grid->width;
    int height = grid->height;
    int x;
    int y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int cell = y * width + x;
            int inside = grid->alpha[cell] != 0.0f;
            const short *list = inside ? g_asBackdropEdgeOut
                                       : g_asBackdropEdgeIn;
            int count = inside ? outsideCount : insideCount;
            int best = 1 << 28;
            int index;
            float distance;

            for (index = 0; index < count; index++) {
                int dx = list[index * 2] - x;
                int dy = list[index * 2 + 1] - y;
                int squared = dx * dx + dy * dy;

                if (squared < best)
                    best = squared;
            }
            distance = sqrtf((float)best) - 0.5f;
            raw[cell] = inside ? distance : -distance;
        }
    }
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            float sum = 0.0f;
            float weight = 0.0f;
            int dx;
            int dy;

            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    float k = g_afBlurKernel[(dy + 1) * 3 + dx + 1];

                    nx = nx < 0 ? 0 : nx >= width ? width - 1 : nx;
                    ny = ny < 0 ? 0 : ny >= height ? height - 1 : ny;
                    sum += raw[ny * width + nx] * k;
                    weight += k;
                }
            }
            grid->distance[y * width + x] = sum / weight;
        }
    }
}

/* Labels each separate body (eight-connected) and returns how many there
 * are, or -1 if there are more than a planet and its moons. */
static int LabelGridBodies(BackdropGrid *grid)
{
    int *stack = g_aiBackdropScratch;
    int cells = grid->width * grid->height;
    int count = 0;
    int cell;

    for (cell = 0; cell < cells; cell++)
        grid->body[cell] = -1;
    for (cell = 0; cell < cells; cell++) {
        int top = 0;
        int size = 0;

        if (grid->alpha[cell] == 0.0f || grid->body[cell] >= 0)
            continue;
        grid->body[cell] = (short)count;
        stack[top++] = cell;
        while (top > 0) {
            int current = stack[--top];
            int x = current % grid->width;
            int y = current / grid->width;
            int dx;
            int dy;

            size++;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    int neighbour;

                    if (nx < 0 || ny < 0 || nx >= grid->width ||
                        ny >= grid->height)
                        continue;
                    neighbour = ny * grid->width + nx;
                    if (grid->alpha[neighbour] == 0.0f ||
                        grid->body[neighbour] >= 0)
                        continue;
                    grid->body[neighbour] = (short)count;
                    stack[top++] = neighbour;
                }
            }
        }
        /* Specks of a few pixels are left out of the fit. */
        if (size < 6) {
            int other;

            for (other = 0; other < cells; other++) {
                if (grid->body[other] == count)
                    grid->body[other] = -2;
            }
            continue;
        }
        count++;
        if (count > BACKDROP_MAX_BODIES)
            return -1;
    }
    return count;
}

/* Fits the limb of one body with its outline squeezed by stretch: random
 * circles through three outline cells, scored by how many outline cells lie
 * on them less a penalty for any of the body spilling outside.  The limb's
 * diameter must be the body's longest side. */
static int FitLimbAtStretch(const BackdropGrid *grid, int bodyIndex,
                            int insideCount, float stretch, float diameter,
                            BackdropBody *body, float *score)
{
    unsigned int seed = 12345U;
    int edges[BACKDROP_MAX_CELLS];
    int edgeCount = 0;
    int opaqueCount = 0;
    int bestInliers = 0;
    float bestScore = -1e9f;
    float bestX = 0.0f;
    float bestY = 0.0f;
    float bestRadius = 0.0f;
    int found = 0;
    int iteration;
    int index;
    int cell;

    for (index = 0; index < insideCount; index++) {
        int x = g_asBackdropEdgeIn[index * 2];
        int y = g_asBackdropEdgeIn[index * 2 + 1];

        if (grid->body[y * grid->width + x] == bodyIndex)
            edges[edgeCount++] = index;
    }
    for (cell = 0; cell < grid->width * grid->height; cell++) {
        if (grid->body[cell] == bodyIndex)
            opaqueCount++;
    }
    if (edgeCount < 3)
        return 0;
    for (iteration = 0; iteration < 1500; iteration++) {
        float px[3];
        float py[3];
        float d;
        float centreX;
        float centreY;
        float radius;
        float candidate;
        int inliers = 0;
        int outside = 0;
        int pick;

        for (pick = 0; pick < 3; pick++) {
            int chosen;

            seed = seed * 1103515245U + 12345U;
            chosen = edges[(seed >> 8) % (unsigned int)edgeCount];
            px[pick] = g_asBackdropEdgeIn[chosen * 2] / stretch;
            py[pick] = g_asBackdropEdgeIn[chosen * 2 + 1];
        }
        d = 2.0f * (px[0] * (py[1] - py[2]) + px[1] * (py[2] - py[0]) +
                    px[2] * (py[0] - py[1]));
        if (fabsf(d) < 1e-6f)
            continue;
        centreX = ((px[0] * px[0] + py[0] * py[0]) * (py[1] - py[2]) +
                   (px[1] * px[1] + py[1] * py[1]) * (py[2] - py[0]) +
                   (px[2] * px[2] + py[2] * py[2]) * (py[0] - py[1])) / d;
        centreY = ((px[0] * px[0] + py[0] * py[0]) * (px[2] - px[1]) +
                   (px[1] * px[1] + py[1] * py[1]) * (px[0] - px[2]) +
                   (px[2] * px[2] + py[2] * py[2]) * (px[1] - px[0])) / d;
        radius = hypotf(px[0] - centreX, py[0] - centreY);
        if (radius + 0.5f < diameter * 0.5f - 1.5f ||
            radius + 0.5f > diameter * 0.5f + 2.0f)
            continue;
        for (index = 0; index < edgeCount; index++) {
            float ex = g_asBackdropEdgeIn[edges[index] * 2] / stretch;
            float ey = g_asBackdropEdgeIn[edges[index] * 2 + 1];

            if (fabsf(hypotf(ex - centreX, ey - centreY) - radius) < 0.75f)
                inliers++;
        }
        for (cell = 0; cell < grid->width * grid->height; cell++) {
            float cx;
            float cy;

            if (grid->body[cell] != bodyIndex)
                continue;
            cx = (float)(cell % grid->width) / stretch;
            cy = (float)(cell / grid->width);
            if (hypotf(cx - centreX, cy - centreY) > radius + 1.2f)
                outside++;
        }
        candidate = (float)inliers - 4.0f * (float)outside;
        if (!found || candidate > bestScore) {
            found = 1;
            bestScore = candidate;
            bestInliers = inliers;
            bestX = centreX;
            bestY = centreY;
            bestRadius = radius;
            if (outside > opaqueCount / 50)
                bestInliers = 0;
        }
    }
    if (!found)
        return 0;
    /* A limb runs through much of the outline and covers a good part of
     * the circle; nothing may spill out of it. */
    if ((float)bestInliers <= 0.4f * (float)edgeCount ||
        (float)bestInliers <= 0.3f * 6.2831853f * (bestRadius + 0.5f))
        return 0;
    body->x = bestX * stretch;
    body->y = bestY;
    body->radius = bestRadius + 0.5f;
    body->stretch = stretch;
    *score = bestScore;
    return 1;
}

static int FitLimb(const BackdropGrid *grid, int bodyIndex, int insideCount,
                   BackdropBody *body)
{
    static const float stretches[5] = { 1.0f, 1.08f, 1.16f, 1.24f, 1.32f };
    int left = grid->width;
    int right = -1;
    int top = grid->height;
    int bottom = -1;
    int found = 0;
    float bestScore = 0.0f;
    int cell;
    int index;

    for (cell = 0; cell < grid->width * grid->height; cell++) {
        int x = cell % grid->width;
        int y = cell / grid->width;

        if (grid->body[cell] != bodyIndex)
            continue;
        left = x < left ? x : left;
        right = x > right ? x : right;
        top = y < top ? y : top;
        bottom = y > bottom ? y : bottom;
    }
    for (index = 0; index < 5; index++) {
        BackdropBody candidate;
        float width = (float)(right - left + 1) / stretches[index];
        float height = (float)(bottom - top + 1);
        float score;

        if (!FitLimbAtStretch(grid, bodyIndex, insideCount, stretches[index],
                              width > height ? width : height, &candidate,
                              &score))
            continue;
        if (!found || score > bestScore + 2.0f) {
            found = 1;
            bestScore = score;
            *body = candidate;
        }
    }
    return found;
}

/* Where the light comes from: the brightness-weighted centre of the body
 * against its middle.  Also the body's average lit colour. */
static void FindBodyLight(const BackdropGrid *grid, BackdropBody *body)
{
    float sumX = 0.0f;
    float sumY = 0.0f;
    float total = 0.0f;
    float mean[3] = { 0.0f, 0.0f, 0.0f };
    float length;
    int cell;
    int channel;

    for (cell = 0; cell < grid->width * grid->height; cell++) {
        float x = (float)(cell % grid->width);
        float y = (float)(cell / grid->width);
        float luma;

        if (grid->alpha[cell] == 0.0f)
            continue;
        if (hypotf((x - body->x) / body->stretch, y - body->y) >
            body->radius + 1.0f)
            continue;
        luma = 0.3f * grid->colour[0][cell] + 0.59f * grid->colour[1][cell] +
               0.11f * grid->colour[2][cell];
        sumX += luma * (x - body->x) / body->stretch;
        sumY += luma * (y - body->y);
        for (channel = 0; channel < 3; channel++)
            mean[channel] += grid->colour[channel][cell] * luma;
        total += luma;
    }
    if (total <= 0.0f)
        total = 1.0f;
    length = hypotf(sumX, sumY);
    if (length <= 0.0f)
        length = 1.0f;
    body->lightX = sumX / length;
    body->lightY = sumY / length;
    body->lightStrength = length / (total * body->radius * 0.15f + 1e-6f);
    if (body->lightStrength > 1.0f)
        body->lightStrength = 1.0f;
    for (channel = 0; channel < 3; channel++)
        body->mean[channel] = mean[channel] / total;
}

static int BuildGrid(BackdropGrid *grid, const unsigned char *pixels,
                     int spriteWidth, int spriteHeight,
                     const unsigned char *quads)
{
    int x;
    int y;

    grid->width = spriteWidth + BACKDROP_PAD * 2;
    grid->height = spriteHeight + BACKDROP_PAD * 2;
    if (grid->width > BACKDROP_MAX_SIDE || grid->height > BACKDROP_MAX_SIDE)
        return 0;
    for (y = 0; y < grid->height; y++) {
        for (x = 0; x < grid->width; x++) {
            int cell = y * grid->width + x;
            int sx = x - BACKDROP_PAD;
            int sy = y - BACKDROP_PAD;
            unsigned char entry = BACKDROP_TRANSPARENT;

            if (sx >= 0 && sy >= 0 && sx < spriteWidth && sy < spriteHeight)
                entry = pixels[sy * spriteWidth + sx];
            grid->index[cell] = entry;
            grid->alpha[cell] = entry == BACKDROP_TRANSPARENT ? 0.0f : 1.0f;
            grid->colour[0][cell] = grid->colour[1][cell] =
                grid->colour[2][cell] = 0.0f;
            if (entry != BACKDROP_TRANSPARENT) {
                grid->colour[0][cell] = quads[entry * 4 + 2] / 255.0f;
                grid->colour[1][cell] = quads[entry * 4 + 1] / 255.0f;
                grid->colour[2][cell] = quads[entry * 4] / 255.0f;
            }
        }
    }
    return 1;
}

static void PutPixel(unsigned char *out, int pixel, float red, float green,
                     float blue, float alpha)
{
    alpha = Clamp(alpha, 0.0f, 1.0f);
    out[pixel * 4] = (unsigned char)(Clamp(red, 0.0f, 1.0f) * alpha * 255.0f +
                                     0.5f);
    out[pixel * 4 + 1] =
        (unsigned char)(Clamp(green, 0.0f, 1.0f) * alpha * 255.0f + 0.5f);
    out[pixel * 4 + 2] =
        (unsigned char)(Clamp(blue, 0.0f, 1.0f) * alpha * 255.0f + 0.5f);
    out[pixel * 4 + 3] = (unsigned char)(alpha * 255.0f + 0.5f);
}

/* Light on black: half of each pixel's own colour kept, half blurred over
 * its neighbours, and opacity taken from the brightest channel. */
static void RenderLight(BackdropGrid *grid, int scale, unsigned char *out)
{
    int width = grid->width;
    int height = grid->height;
    int outWidth = width * scale;
    int outHeight = height * scale;
    float *premultiplied[3];
    float *opacity = g_afBackdropScratch;
    int x;
    int y;

    /* The smooth planes are free for this treatment. */
    premultiplied[0] = grid->smooth[0];
    premultiplied[1] = grid->smooth[1];
    premultiplied[2] = grid->smooth[2];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int cell = y * width + x;
            float blurred[3] = { 0.0f, 0.0f, 0.0f };
            float weight = 0.0f;
            int dx;
            int dy;
            int channel;
            float brightest;

            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int nx = x + dx;
                    int ny = y + dy;
                    int neighbour;
                    float k = g_afBlurKernel[(dy + 1) * 3 + dx + 1];

                    nx = nx < 0 ? 0 : nx >= width ? width - 1 : nx;
                    ny = ny < 0 ? 0 : ny >= height ? height - 1 : ny;
                    neighbour = ny * width + nx;
                    weight += k;
                    for (channel = 0; channel < 3; channel++)
                        blurred[channel] += grid->colour[channel][neighbour] *
                                            grid->alpha[neighbour] * k;
                }
            }
            brightest = 0.0f;
            for (channel = 0; channel < 3; channel++) {
                float value = grid->colour[channel][cell] *
                                  grid->alpha[cell] * 0.5f +
                              blurred[channel] / weight * 0.5f;

                premultiplied[channel][cell] = value;
                if (value > brightest)
                    brightest = value;
            }
            opacity[cell] = brightest * 1.6f > 1.0f ? 1.0f : brightest * 1.6f;
        }
    }
    for (y = 0; y < outHeight; y++) {
        for (x = 0; x < outWidth; x++) {
            float u = ((float)x + 0.5f) / (float)scale - 0.5f;
            float v = ((float)y + 0.5f) / (float)scale - 0.5f;
            float alpha =
                Clamp(SampleBicubic(opacity, width, height, u, v), 0.0f, 1.0f);

            if (alpha < 0.004f) {
                PutPixel(out, y * outWidth + x, 0.0f, 0.0f, 0.0f, 0.0f);
                continue;
            }
            PutPixel(out, y * outWidth + x,
                     SampleBicubic(premultiplied[0], width, height, u, v) /
                         alpha,
                     SampleBicubic(premultiplied[1], width, height, u, v) /
                         alpha,
                     SampleBicubic(premultiplied[2], width, height, u, v) /
                         alpha,
                     alpha);
        }
    }
}

/* Smoothed colour inside an outline redrawn a display pixel wide. */
static void RenderClean(const BackdropGrid *grid, int scale,
                        unsigned char *out)
{
    int outWidth = grid->width * scale;
    int outHeight = grid->height * scale;
    int x;
    int y;

    for (y = 0; y < outHeight; y++) {
        for (x = 0; x < outWidth; x++) {
            float u = ((float)x + 0.5f) / (float)scale - 0.5f;
            float v = ((float)y + 0.5f) / (float)scale - 0.5f;
            float alpha = Clamp(SampleBicubic(grid->distance, grid->width,
                                              grid->height, u, v) *
                                        (float)scale +
                                    0.5f,
                                0.0f, 1.0f);

            if (alpha <= 0.0f) {
                PutPixel(out, y * outWidth + x, 0.0f, 0.0f, 0.0f, 0.0f);
                continue;
            }
            PutPixel(out, y * outWidth + x,
                     SampleBicubic(grid->smooth[0], grid->width, grid->height,
                                   u, v),
                     SampleBicubic(grid->smooth[1], grid->width, grid->height,
                                   u, v),
                     SampleBicubic(grid->smooth[2], grid->width, grid->height,
                                   u, v),
                     alpha);
        }
    }
}

static void RenderSphereBody(const BackdropGrid *grid, int scale,
                             const BackdropBody *body, unsigned char *out)
{
    const float night = 0.04f;
    int outWidth = grid->width * scale;
    int outHeight = grid->height * scale;
    float radius = body->radius;
    float rim = radius * 0.09f > 0.9f ? radius * 0.09f : 0.9f;
    float atmosphere[3];
    int left;
    int right;
    int top;
    int bottom;
    int x;
    int y;

    atmosphere[0] = body->mean[0] * 0.6f + 0.4f;
    atmosphere[1] = body->mean[1] * 0.6f + 0.45f;
    atmosphere[2] = body->mean[2] * 0.6f + 0.55f;
    left = (int)floorf((body->x - (radius + rim + 1.0f) * body->stretch) *
                       (float)scale);
    right = (int)ceilf((body->x + (radius + rim + 2.0f) * body->stretch) *
                       (float)scale);
    top = (int)floorf((body->y - radius - rim - 1.0f) * (float)scale);
    bottom = (int)ceilf((body->y + radius + rim + 2.0f) * (float)scale);
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > outWidth ? outWidth : right;
    bottom = bottom > outHeight ? outHeight : bottom;
    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            float u = ((float)x + 0.5f) / (float)scale - 0.5f;
            float v = ((float)y + 0.5f) / (float)scale - 0.5f;
            float dx = (u - body->x) / body->stretch;
            float dy = v - body->y;
            float distance = hypotf(dx, dy);
            float disc;
            float side;
            float t;
            float glow;
            float colour[3] = { 0.0f, 0.0f, 0.0f };
            float alpha;
            int pixel = y * outWidth + x;
            int channel;

            if (distance > radius + rim + 1.0f)
                continue;
            disc = Clamp((radius - distance) * (float)scale + 0.5f, 0.0f,
                         1.0f);
            side = 1.0f - body->lightStrength +
                   body->lightStrength *
                       SmoothStep(-0.35f, 0.7f,
                                  (dx * body->lightX + dy * body->lightY) /
                                      (distance > 0.0f ? distance : 1.0f));
            t = Clamp((distance - radius) / rim, 0.0f, 1.0f);
            glow = distance > radius - 1.0f
                       ? 0.55f * (1.0f - t) * (1.0f - t) * side
                       : 0.0f;
            if (disc > 0.0f) {
                /* The sprite's own outline marks the terminator; pushed out
                 * a little so it never darkens the fitted limb. */
                float lit = Clamp(
                    (SampleBicubic(grid->distance, grid->width, grid->height,
                                   u, v) +
                     1.2f) * (float)scale * 0.35f +
                        0.5f,
                    0.0f, 1.0f);
                float edge = SmoothStep(radius - rim * 1.2f, radius,
                                        distance) *
                             side * 0.35f * lit;

                for (channel = 0; channel < 3; channel++) {
                    float painted = Clamp(
                        SampleBicubic(grid->smooth[channel], grid->width,
                                      grid->height, u, v),
                        0.0f, 1.0f);
                    float dark = body->mean[channel] * night;

                    colour[channel] = dark + (painted - dark) * lit;
                    colour[channel] +=
                        (atmosphere[channel] - colour[channel]) * edge;
                }
            }
            alpha = disc + glow * (1.0f - disc);
            if (alpha < 0.004f || alpha * 255.0f < (float)out[pixel * 4 + 3])
                continue;
            for (channel = 0; channel < 3; channel++)
                colour[channel] = (colour[channel] * disc +
                                   atmosphere[channel] * glow * (1.0f - disc)) /
                                  alpha;
            PutPixel(out, pixel, colour[0], colour[1], colour[2], alpha);
        }
    }
}

static int RenderSphere(BackdropGrid *grid, int scale, int insideCount,
                        unsigned char *out)
{
    int count = LabelGridBodies(grid);
    int index;

    if (count <= 0)
        return 0;
    grid->bodyCount = count;
    for (index = 0; index < count; index++) {
        if (!FitLimb(grid, index, insideCount, &grid->bodies[index]))
            return 0;
        FindBodyLight(grid, &grid->bodies[index]);
    }
    memset(out, 0, (size_t)grid->width * scale * grid->height * scale * 4U);
    for (index = 0; index < count; index++)
        RenderSphereBody(grid, scale, &grid->bodies[index], out);
    return 1;
}

/* The ringed planet's scene, in sprite pixels with pixel centres on whole
 * numbers, fitted to its 49x49 sprite.  The ring is a flat circle seen
 * nearly edge-on along the sprite's diagonal; its far half is on the side
 * of +minor. */
#define RINGED_SPRITE_SIDE 49
#define RINGED_CENTRE_X 23.5f
#define RINGED_CENTRE_Y 24.5f
#define RINGED_PLANET_RADIUS 12.4f
#define RINGED_OUTER 34.0f
#define RINGED_INNER 19.0f
/* How wide the ring's ellipse is for its length: the sine of its tilt. */
#define RINGED_TILT_SINE 0.22f
/* Where the light comes from, in degrees round the ring's plane from its
 * major axis; this places the planet's shadow across the far half. */
#define RINGED_LIGHT_DEGREES 26.0f
#define RINGED_SUBSAMPLES 4

static const float g_afRingedMajor[2] = { 0.722f, -0.692f };
static const float g_afRingedMinor[2] = { 0.692f, 0.722f };

static void EntryColour(const unsigned char *quads, int entry, float rgb[3])
{
    rgb[0] = quads[entry * 4 + 2] / 255.0f;
    rgb[1] = quads[entry * 4 + 1] / 255.0f;
    rgb[2] = quads[entry * 4] / 255.0f;
}

static void MixColour(const float a[3], const float b[3], float t,
                      float out[3])
{
    int channel;

    for (channel = 0; channel < 3; channel++)
        out[channel] = a[channel] + (b[channel] - a[channel]) * t;
}

typedef struct RingedScene {
    float light[3];
    float pole[3];
    float tiltCosine;
    float bandLight[3];
    float bandMid[3];
    float bandDark[3];
    float ringLight[3];
    float ringMid[3];
    float ringDark[3];
} RingedScene;

/* Bands run parallel to the ring: a mix of two waves over latitude, darker
 * toward the poles. */
static void RingedPlanetAlbedo(const RingedScene *scene, float latitude,
                               float out[3])
{
    float wave = sinf(latitude * 3.14159265f * 5.2f + 0.6f) * 0.6f +
                 sinf(latitude * 3.14159265f * 11.3f + 1.7f) * 0.4f;

    if (wave > 0.0f)
        MixColour(scene->bandMid, scene->bandLight,
                  SmoothStep(0.0f, 0.7f, wave), out);
    else
        MixColour(scene->bandMid, scene->bandDark,
                  SmoothStep(0.0f, 0.7f, -wave), out);
    MixColour(out, scene->bandDark,
              SmoothStep(0.55f, 1.0f, fabsf(latitude)) * 0.5f, out);
}

/* A brighter middle and a thin dark gap near the outer edge. */
static float RingedRingAlbedo(const RingedScene *scene, float radius,
                              float out[3])
{
    float t = (radius - RINGED_INNER) / (RINGED_OUTER - RINGED_INNER);

    MixColour(scene->ringMid, scene->ringLight,
              SmoothStep(0.15f, 0.45f, t) *
                  (1.0f - SmoothStep(0.6f, 0.75f, t)),
              out);
    MixColour(out, scene->ringDark,
              SmoothStep(0.78f, 0.84f, t) *
                  (1.0f - SmoothStep(0.86f, 0.92f, t)) * 0.8f,
              out);
    return SmoothStep(0.0f, 0.06f, t) * (1.0f - SmoothStep(0.94f, 1.0f, t));
}

/* One point of the scene: its colour and coverage. */
static float SampleRinged(const RingedScene *scene, float x, float y,
                          float colour[3])
{
    float dx = x - RINGED_CENTRE_X;
    float dy = y - RINGED_CENTRE_Y;
    float major = dx * g_afRingedMajor[0] + dy * g_afRingedMajor[1];
    float minor = dx * g_afRingedMinor[0] + dy * g_afRingedMinor[1];
    float across = minor / RINGED_TILT_SINE;
    float radius = hypotf(major, across);
    float ringDepth = -across * scene->tiltCosine;
    float ringColour[3];
    float ringAlpha = 0.0f;
    float squared = major * major + minor * minor;
    int channel;

    if (radius > RINGED_INNER - 0.5f && radius < RINGED_OUTER + 0.5f) {
        float point[3];
        float along;
        float off;
        float lit = 1.0f;

        point[0] = major;
        point[1] = across * RINGED_TILT_SINE;
        point[2] = ringDepth;
        along = point[0] * scene->light[0] + point[1] * scene->light[1] +
                point[2] * scene->light[2];
        if (along < 0.0f) {
            off = hypotf(hypotf(point[0] - along * scene->light[0],
                                point[1] - along * scene->light[1]),
                         point[2] - along * scene->light[2]);
            lit = SmoothStep(RINGED_PLANET_RADIUS - 0.6f,
                             RINGED_PLANET_RADIUS + 0.6f, off);
        }
        ringAlpha = RingedRingAlbedo(scene, radius, ringColour);
        for (channel = 0; channel < 3; channel++)
            ringColour[channel] *= 0.08f + 0.92f * lit;
    }
    if (squared < RINGED_PLANET_RADIUS * RINGED_PLANET_RADIUS) {
        float depth = sqrtf(RINGED_PLANET_RADIUS * RINGED_PLANET_RADIUS -
                            squared);
        float latitude;
        float lambert;
        float light;

        if (ringAlpha > 0.0f && ringDepth > depth) {
            memcpy(colour, ringColour, sizeof(ringColour));
            return ringAlpha;
        }
        latitude = (major * scene->pole[0] + minor * scene->pole[1] +
                    depth * scene->pole[2]) / RINGED_PLANET_RADIUS;
        lambert = (major * scene->light[0] + minor * scene->light[1] +
                   depth * scene->light[2]) / RINGED_PLANET_RADIUS;
        light = 0.04f + Clamp(lambert * 1.4f + 0.12f, 0.0f, 1.0f) * 1.05f;
        RingedPlanetAlbedo(scene, latitude, colour);
        for (channel = 0; channel < 3; channel++)
            colour[channel] *= light;
        return 1.0f;
    }
    memcpy(colour, ringColour, sizeof(ringColour));
    return ringAlpha;
}

static int RenderRinged(const BackdropGrid *grid, int scale,
                        const unsigned char *quads, unsigned char *out)
{
    RingedScene scene;
    float angle = RINGED_LIGHT_DEGREES * 3.14159265f / 180.0f;
    float length;
    float colourA[3];
    float colourB[3];
    int outWidth = grid->width * scale;
    int outHeight = grid->height * scale;
    int x;
    int y;

    if (grid->width != RINGED_SPRITE_SIDE + BACKDROP_PAD * 2 ||
        grid->height != RINGED_SPRITE_SIDE + BACKDROP_PAD * 2)
        return 0;
    scene.tiltCosine = sqrtf(1.0f - RINGED_TILT_SINE * RINGED_TILT_SINE);
    scene.pole[0] = 0.0f;
    scene.pole[1] = scene.tiltCosine;
    scene.pole[2] = RINGED_TILT_SINE;
    scene.light[0] = -cosf(angle);
    scene.light[1] = -sinf(angle) * RINGED_TILT_SINE;
    scene.light[2] = sinf(angle) * scene.tiltCosine;
    length = sqrtf(scene.light[0] * scene.light[0] +
                   scene.light[1] * scene.light[1] +
                   scene.light[2] * scene.light[2]);
    scene.light[0] /= length;
    scene.light[1] /= length;
    scene.light[2] /= length;
    /* Colours from the sprite's own palette entries, so fades carry over. */
    EntryColour(quads, 214, scene.bandLight);
    EntryColour(quads, 216, colourA);
    EntryColour(quads, 217, colourB);
    MixColour(colourA, colourB, 0.5f, scene.bandMid);
    EntryColour(quads, 219, scene.bandDark);
    EntryColour(quads, 53, scene.ringLight);
    EntryColour(quads, 54, colourA);
    EntryColour(quads, 55, colourB);
    MixColour(colourA, colourB, 0.5f, scene.ringMid);
    EntryColour(quads, 56, scene.ringDark);

    for (y = 0; y < outHeight; y++) {
        for (x = 0; x < outWidth; x++) {
            float sum[3] = { 0.0f, 0.0f, 0.0f };
            float coverage = 0.0f;
            int i;
            int j;

            for (j = 0; j < RINGED_SUBSAMPLES; j++) {
                for (i = 0; i < RINGED_SUBSAMPLES; i++) {
                    float sample[3];
                    float sx = ((float)x + ((float)i + 0.5f) /
                                               RINGED_SUBSAMPLES) /
                                   (float)scale -
                               0.5f - BACKDROP_PAD;
                    float sy = ((float)y + ((float)j + 0.5f) /
                                               RINGED_SUBSAMPLES) /
                                   (float)scale -
                               0.5f - BACKDROP_PAD;
                    float alpha = SampleRinged(&scene, sx, sy, sample);

                    sum[0] += sample[0] * alpha;
                    sum[1] += sample[1] * alpha;
                    sum[2] += sample[2] * alpha;
                    coverage += alpha;
                }
            }
            if (coverage <= 0.0f) {
                PutPixel(out, y * outWidth + x, 0.0f, 0.0f, 0.0f, 0.0f);
                continue;
            }
            PutPixel(out, y * outWidth + x, sum[0] / coverage,
                     sum[1] / coverage, sum[2] / coverage,
                     coverage / (RINGED_SUBSAMPLES * RINGED_SUBSAMPLES));
        }
    }
    return 1;
}

static void SumEntryColour(const BackdropImage *image,
                           const unsigned char *quads, float sum[3])
{
    int entry;

    sum[0] = sum[1] = sum[2] = 0.0f;
    for (entry = 0; entry < 256; entry++) {
        if (image->entryCount[entry] == 0)
            continue;
        sum[0] += (float)image->entryCount[entry] * quads[entry * 4 + 2];
        sum[1] += (float)image->entryCount[entry] * quads[entry * 4 + 1];
        sum[2] += (float)image->entryCount[entry] * quads[entry * 4];
    }
}

/*
 * The small sun (shape 9) is vertically symmetric except that its sprite
 * stops one row short: its last row repeats row 1, and the copy of row 0
 * that should close the rim is missing. Restore that row by mirroring, but
 * only when the frame has the exact layout this assumes.
 */
static void RestoreClippedBottomRow(short shapeId, unsigned char *pixels,
                                   short width, short *height)
{
    if (shapeId != 9 || width != 27 || *height != 26 ||
        *height + 1 + BACKDROP_PAD * 2 > BACKDROP_MAX_SIDE)
        return;
    if (memcmp(pixels + (size_t)width * (size_t)(*height - 1),
               pixels + (size_t)width, (size_t)width) != 0)
        return;
    memcpy(pixels + (size_t)width * (size_t)*height, pixels, (size_t)width);
    (*height)++;
}

static void BuildImage(BackdropImage *image, struct Shape *shape,
                       short shapeId, short frame, int look,
                       const unsigned char *quads)
{
    BackdropGrid *grid = &g_stBackdropGrid;
    unsigned char *rgba;
    short width = 0;
    short height = 0;
    short originX = 0;
    short originY = 0;
    int insideCount;
    int outsideCount;
    int built;
    int cell;

    image->failed = 1;
    image->handle = 0;
    GetShapeFrameExtents(shape, frame, &width, &height, &originX, &originY);
    if (width <= 0 || height <= 0 ||
        width + BACKDROP_PAD * 2 > BACKDROP_MAX_SIDE ||
        height + BACKDROP_PAD * 2 > BACKDROP_MAX_SIDE)
        return;
    memset(g_abBackdropPixels, BACKDROP_TRANSPARENT,
           (size_t)width * (size_t)height);
    DecodeShapeFrame(shape, frame, g_abBackdropPixels, width, height,
                     originX, originY);
    RestoreClippedBottomRow(shapeId, g_abBackdropPixels, width, &height);
    if (!BuildGrid(grid, g_abBackdropPixels, width, height, quads))
        return;
    image->spriteWidth = width;
    image->spriteHeight = height;
    image->originX = originX;
    image->originY = originY;
    memset(image->entryCount, 0, sizeof(image->entryCount));
    for (cell = 0; cell < (int)width * height; cell++) {
        if (g_abBackdropPixels[cell] != BACKDROP_TRANSPARENT)
            image->entryCount[g_abBackdropPixels[cell]]++;
    }
    SumEntryColour(image, quads, image->builtColour);
    SmoothGridColour(grid);
    FindGridEdges(grid, &insideCount, &outsideCount);
    BuildGridDistance(grid, insideCount, outsideCount);
    image->width = grid->width * image->scale;
    image->height = grid->height * image->scale;
    rgba = malloc((size_t)image->width * (size_t)image->height * 4U);
    if (rgba == 0)
        return;
    built = 1;
    if (look == LOOK_RINGED)
        built = RenderRinged(grid, image->scale, quads, rgba);
    else if (look == LOOK_SPHERE)
        built = RenderSphere(grid, image->scale, insideCount, rgba);
    else if (look == LOOK_CLEAN)
        RenderClean(grid, image->scale, rgba);
    else
        RenderLight(grid, image->scale, rgba);
    if (built) {
        image->handle =
            SdlGlRendererCreateImage(image->width, image->height, rgba);
        image->failed = image->handle == 0;
    }
    free(rgba);
}

static BackdropImage *GetImage(short shapeId, int scale, struct Shape *shape,
                               short frame, const unsigned char *quads)
{
    unsigned int generation = SdlGlRendererImageGeneration();
    BackdropImage *image = 0;
    BackdropImage *oldest = &g_aBackdropImages[0];
    int index;

    g_uBackdropUse++;
    for (index = 0; index < BACKDROP_CACHE_CAPACITY; index++) {
        BackdropImage *entry = &g_aBackdropImages[index];

        if (entry->generation == generation && entry->scale == scale &&
            entry->shapeId == shapeId && (entry->handle != 0 || entry->failed))
            image = entry;
        if (entry->lastUse < oldest->lastUse)
            oldest = entry;
    }
    if (image != 0) {
        float now[3];

        /* Built during a fade: rebuild once the palette is well brighter. */
        SumEntryColour(image, quads, now);
        if (image->failed ||
            now[0] + now[1] + now[2] <=
                (image->builtColour[0] + image->builtColour[1] +
                 image->builtColour[2]) * 1.2f + 1.0f) {
            image->lastUse = g_uBackdropUse;
            return image;
        }
        oldest = image;
    }
    if (oldest->handle != 0 && oldest->generation == generation)
        SdlGlRendererReleaseImage(oldest->handle);
    memset(oldest, 0, sizeof(*oldest));
    oldest->shapeId = shapeId;
    oldest->scale = scale;
    oldest->generation = generation;
    oldest->lastUse = g_uBackdropUse;
    BuildImage(oldest, shape, shapeId, frame, g_abBackdropLook[shapeId],
               quads);
    return oldest;
}

/* The suns and stars among the backdrop shapes. */
static int IsBackdropSun(short shapeId)
{
    switch (shapeId) {
    case 4:  /* white star */
    case 6:  /* red sun */
    case 9:  /* yellow sun */
    case 10: /* red giant */
    case 14: /* large yellow sun */
    case 15: /* small yellow sun */
    case 18: /* speckled red sun */
    case 21: /* blue-white star */
    case 22: /* small blue-white star */
        return 1;
    default:
        return 0;
    }
}

int SdlFineBackdropDraw(const Viewport *viewport, short obj, float x,
                        float y, struct Shape *shape, short frame,
                        short angle, short scale, short flip)
{
    const unsigned char *quads;
    BackdropImage *image;
    short shapeId;
    float spriteScale;
    float now[3];
    float tint[3];
    float corners[8];
    float left;
    float top;
    float right;
    float bottom;
    float cosine;
    float sine;
    float scaleX;
    float scaleY;
    int fixedCosine;
    int fixedSine;
    int imageScale;
    int channel;
    int corner;

    if (!g_bFineBackdropEnabled || SdlSwcFlightActive() || viewport == 0 ||
        shape == 0 || obj < 0 || obj >= SPACE_OBJECT_COUNT ||
        aeObjectClass[obj] != OBJECT_CLASS_PLANET)
        return 0;
    if (g_apObjectShape[obj] != shape || frame != 0 || scale <= 0 ||
        (flip != 0 && flip != 0x10 && flip != 0x20 && flip != 0x30))
        return 0;
    shapeId = g_asObjectShapeId[obj];
    if (shapeId < 0 || shapeId >= BACKDROP_SHAPE_COUNT ||
        g_abBackdropLook[shapeId] == LOOK_SPRITE)
        return 0;
    if (!SdlUsingGlRenderer() || SdlEgaDitherEnabled())
        return 0;
    if (SdlShip3dEnabled() && !SdlShip3dUsesGl())
        return 0;

    spriteScale = (float)scale / 256.0f;
    imageScale = (int)ceilf(SdlGlRendererOutputScale() * spriteScale - 0.1f);
    if (imageScale < 1)
        imageScale = 1;
    if (imageScale > BACKDROP_MAX_SCALE)
        imageScale = BACKDROP_MAX_SCALE;
    quads = SdlGetPaletteQuads();
    image = GetImage(shapeId, imageScale, shape, frame, quads);
    if (image == 0 || image->failed || image->handle == 0)
        return 0;

    SumEntryColour(image, quads, now);
    for (channel = 0; channel < 3; channel++) {
        tint[channel] = image->builtColour[channel] > 0.0f
                            ? now[channel] / image->builtColour[channel]
                            : 1.0f;
        tint[channel] = Clamp(tint[channel], 0.0f, 1.5f);
    }
    /* The sprite's pixel (0, 0) covers -origin - 0.5 to -origin + 0.5 about
     * the object's position; the image adds a margin around it.  The quad
     * is scaled, flipped and turned about the position as the sprite path
     * does: the backdrop turns with the camera's roll. */
    left = -(float)image->originX - 0.5f - BACKDROP_PAD;
    top = -(float)image->originY - 0.5f - BACKDROP_PAD;
    right = left + (float)(image->spriteWidth + BACKDROP_PAD * 2);
    bottom = top + (float)(image->spriteHeight + BACKDROP_PAD * 2);
    GetRLETransformTrig(angle * 10, &fixedCosine, &fixedSine);
    cosine = (float)fixedCosine / 65536.0f;
    sine = (float)fixedSine / 65536.0f;
    scaleX = (flip & 0x10) != 0 ? -spriteScale : spriteScale;
    scaleY = (flip & 0x20) != 0 ? -spriteScale : spriteScale;
    for (corner = 0; corner < 4; corner++) {
        float sourceX = ((corner & 2) != 0 ? right : left) * scaleX;
        float sourceY = ((corner & 1) != 0 ? bottom : top) * scaleY;

        corners[corner * 2] = x + sourceX * cosine - sourceY * sine;
        corners[corner * 2 + 1] = y + sourceX * sine + sourceY * cosine;
    }
    if (!SdlGlRendererRecordImage(viewport, image->handle, corners, tint))
        return 0;
    /* --hdr: a sun's or star's hot core glows above white. */
    if (IsBackdropSun(shapeId))
        SdlGlRendererSetLastHdrGain(SdlHdrGain());
    return 1;
}
