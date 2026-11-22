/* Fork-only: --glare makes a large backdrop sun dazzle, so a craft flying in
 * front of it is hard to make out.
 *
 * Three parts, all in the GL object layer, so this needs --enhanced:
 *
 *   Veiling glare.  Light from the sun scatters in the eye and lays a haze
 *   over the view: tight and strong round the sun, with a faint wide veil
 *   well beyond it, which reaches into the view even when the sun is just
 *   off screen.  It is drawn after every object, as a translucent layer in
 *   the sun's colour, so what lies under it loses contrast.  The layer is
 *   drawn only where the indexed frame shows open space, so the cockpit,
 *   the HUD and its brackets stay clear.
 *
 *   Silhouettes.  A 3D ship (--ship3d) in front of the sun shows its dark
 *   side: the light at the camera fades out as the ship lines up with the
 *   sun, and the sun rims its edges instead.
 *
 *   Eclipse.  A 3D ship over the sun's disc cuts the glare by the share of
 *   the disc its points cover, measured on a grid over the disc.  The
 *   strength eases towards that over a fraction of a second, so a hull
 *   sliding over the sun dims it smoothly.
 *
 * Only the large suns dazzle (the red sun, the red giant and Secret Missions
 * 2's large yellow sun); their colour comes from a palette entry, so the
 * glare follows palette fades.  The sun is the one --backdrop-light ranks
 * first among large suns.
 *
 * With --hdr and --fine-backdrop, the haze also blooms above white (see
 * hdr.c): at full strength over the sun's disc, falling off past its edge.
 * Over the disc the haze is all but opaque, so without the bloom there it
 * would hide the disc's own hot core under standard white, leaving the
 * corona brighter than the sun.  The bloom falls with the eclipse like the
 * rest of the haze.  Without --fine-backdrop the disc is drawn in the
 * 320x200 frame at standard brightness, and a bloom brighter than the disc
 * would look wrong. */
#include "wc1.h"

#include <math.h>

#include "video_internal.h"

#define GLARE_RINGS 11
#define GLARE_SEGMENTS 48
#define GLARE_FLOATS 7
#define GLARE_MAX_VERTICES (GLARE_SEGMENTS * (3 + (GLARE_RINGS - 1) * 6))
/* The eclipse grid's cells across the sun's diameter. */
#define GLARE_GRID 16
/* How quickly the glare follows the eclipse, in seconds. */
#define GLARE_EASE_SECONDS 0.08f
/* Strength of the tight glow round the sun and of the wide veil. */
#define GLARE_CORE 0.7f
#define GLARE_VEIL 0.3f
/* --hdr: how far out the bloom keeps full strength, and how quickly it
 * falls past that, in sun radii. */
#define GLARE_BLOOM_DISC 0.5f
#define GLARE_BLOOM_FALL 0.6f

typedef struct GlareSun {
    short shapeId;
    /* The palette entry nearest the sprite's bright colour. */
    unsigned char entry;
} GlareSun;

static const GlareSun g_aGlareSuns[] = {
    { 6, 116 },  /* red sun */
    { 10, 135 }, /* red giant */
    { 14, 69 },  /* large yellow sun */
};

static int g_bGlareEnabled;
/* This frame's sun: whether there is one in front of the eye, where it is
 * on screen, its radius there, its direction in the eye's frame and its
 * colour. */
static int g_bGlareActive;
static float g_fGlareX;
static float g_fGlareY;
static float g_fGlareRadius;
static float g_afGlareDirection[3];
static float g_afGlareColour[3];
/* Which cells of the grid over the sun's disc a ship covers this frame. */
static unsigned char g_abGlareCover[GLARE_GRID * GLARE_GRID];
static int g_bGlareCovered;
/* The glare's strength as last drawn, and when. */
static float g_fGlareStrength = 1.0f;
static Uint32 g_uGlareTicks;
static float g_afGlareVertices[GLARE_MAX_VERTICES * GLARE_FLOATS];

void SdlGlareEnable(void)
{
    g_bGlareEnabled = 1;
    SdlBackdropLightTrack();
}

static float GlareSmoothStep(float edge0, float edge1, float value)
{
    float t = (value - edge0) / (edge1 - edge0);

    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* Called before the frame's objects are drawn: finds the sun. */
void SdlGlareBeginFrame(void)
{
    const unsigned char *quads;
    float direction[3];
    float focal;
    short shapeId;
    int size;
    unsigned int index;
    int found = -1;

    g_bGlareActive = 0;
    if (g_bGlareCovered) {
        memset(g_abGlareCover, 0, sizeof(g_abGlareCover));
        g_bGlareCovered = 0;
    }
    if (!g_bGlareEnabled || SdlSwcFlightActive() || !SdlUsingGlRenderer() ||
        SdlEgaDitherEnabled())
        return;
    if (SdlShip3dEnabled() && !SdlShip3dUsesGl())
        return;
    if (!SdlBackdropGlareSun(direction, &shapeId, &size) ||
        direction[2] <= 0.05f)
        return;
    for (index = 0; index < sizeof(g_aGlareSuns) / sizeof(g_aGlareSuns[0]);
         index++) {
        if (g_aGlareSuns[index].shapeId == shapeId)
            found = (int)index;
    }
    if (found < 0)
        return;
    quads = SdlGetPaletteQuads();
    if (quads == 0)
        return;
    focal = SdlFovFocal();
    g_fGlareX = (float)nCurrentViewCenterPosX +
                focal * direction[0] / direction[2];
    g_fGlareY = (float)nCurrentViewCenterPosY +
                focal * direction[1] / direction[2];
    /* Backdrop sprites are drawn at one game pixel per sprite pixel. */
    g_fGlareRadius = (float)size * 0.5f;
    memcpy(g_afGlareDirection, direction, sizeof(direction));
    g_afGlareColour[0] = quads[g_aGlareSuns[found].entry * 4 + 2] / 255.0f;
    g_afGlareColour[1] = quads[g_aGlareSuns[found].entry * 4 + 1] / 255.0f;
    g_afGlareColour[2] = quads[g_aGlareSuns[found].entry * 4] / 255.0f;
    g_bGlareActive = 1;
}

/* For a 3D ship centred at (x, y) on screen with the given radius there:
 * how far it is lined up with the sun (0 to 1), and the sun's direction in
 * the eye's frame and its colour.  0 when nothing dazzles. */
float SdlGlareSilhouette(float x, float y, float radius, float direction[3],
                         float colour[3])
{
    float distance;
    float amount;

    if (!g_bGlareActive)
        return 0.0f;
    distance = hypotf(x - g_fGlareX, y - g_fGlareY);
    amount = 1.0f - GlareSmoothStep(g_fGlareRadius * 0.9f,
                                    g_fGlareRadius * 2.5f + radius * 0.5f,
                                    distance);
    memcpy(direction, g_afGlareDirection, sizeof(g_afGlareDirection));
    memcpy(colour, g_afGlareColour, sizeof(g_afGlareColour));
    return amount;
}

/* A 3D ship's point at (x, y) on screen, drawn halfExtent each way: marks
 * the eclipse grid cells it covers. */
void SdlGlareCoverPoint(float x, float y, float halfExtent)
{
    float cell;
    float left;
    float top;
    int firstX;
    int lastX;
    int firstY;
    int lastY;
    int i;
    int j;

    if (!g_bGlareActive)
        return;
    left = g_fGlareX - g_fGlareRadius;
    top = g_fGlareY - g_fGlareRadius;
    if (x + halfExtent < left || y + halfExtent < top ||
        x - halfExtent > g_fGlareX + g_fGlareRadius ||
        y - halfExtent > g_fGlareY + g_fGlareRadius)
        return;
    cell = g_fGlareRadius * 2.0f / GLARE_GRID;
    /* Cells whose centres the point's square covers. */
    firstX = (int)ceilf((x - halfExtent - left) / cell - 0.5f);
    lastX = (int)floorf((x + halfExtent - left) / cell - 0.5f);
    firstY = (int)ceilf((y - halfExtent - top) / cell - 0.5f);
    lastY = (int)floorf((y + halfExtent - top) / cell - 0.5f);
    if (firstX < 0)
        firstX = 0;
    if (firstY < 0)
        firstY = 0;
    if (lastX >= GLARE_GRID)
        lastX = GLARE_GRID - 1;
    if (lastY >= GLARE_GRID)
        lastY = GLARE_GRID - 1;
    for (j = firstY; j <= lastY; j++) {
        for (i = firstX; i <= lastX; i++)
            g_abGlareCover[j * GLARE_GRID + i] = 1;
    }
    g_bGlareCovered = 1;
}

/* The share of the sun's disc behind the frame's ships. */
static float GetGlareCover(void)
{
    int inside = 0;
    int covered = 0;
    int i;
    int j;

    if (!g_bGlareCovered)
        return 0.0f;
    for (j = 0; j < GLARE_GRID; j++) {
        for (i = 0; i < GLARE_GRID; i++) {
            float u = ((float)i + 0.5f) / GLARE_GRID * 2.0f - 1.0f;
            float v = ((float)j + 0.5f) / GLARE_GRID * 2.0f - 1.0f;

            if (u * u + v * v > 1.0f)
                continue;
            inside++;
            covered += g_abGlareCover[j * GLARE_GRID + i];
        }
    }
    return inside > 0 ? (float)covered / (float)inside : 0.0f;
}

/* The haze's opacity at distance from the sun's centre, in sun radii. */
static float GetGlareOpacity(float distance)
{
    float core = GLARE_CORE * expf(-(distance > 0.5f ? distance - 0.5f : 0.0f) /
                                   0.6f);
    float veil = GLARE_VEIL / (1.0f + (distance / 2.2f) * (distance / 2.2f));
    float opacity = core + veil;

    return opacity > 1.0f ? 1.0f : opacity;
}

/* --hdr: the bloom's light above white at distance from the sun's centre,
 * in sun radii, as a share of the headroom: all of it over the disc. */
static float GetGlareBloom(float distance)
{
    if (distance <= GLARE_BLOOM_DISC)
        return 1.0f;
    return expf(-(distance - GLARE_BLOOM_DISC) / GLARE_BLOOM_FALL);
}

/* A vertex of opacity alpha, with bloom light added over it: the colour is
 * scaled so that, premultiplied, it carries both. */
static float *AppendGlareVertex(float *out, float x, float y,
                                const float colour[3], float alpha,
                                float bloom)
{
    float scale = alpha > 0.0001f ? (alpha + bloom) / alpha : 1.0f;

    out[0] = x;
    out[1] = y;
    out[2] = 0.0f;
    out[3] = colour[0] * scale;
    out[4] = colour[1] * scale;
    out[5] = colour[2] * scale;
    out[6] = alpha;
    return out + GLARE_FLOATS;
}

/* Called after the frame's objects are drawn: lays the haze over them. */
void SdlGlareDraw(const Viewport *viewport)
{
    static const float ringRadius[GLARE_RINGS] = {
        0.5f, 0.8f, 1.1f, 1.5f, 2.0f, 3.0f, 4.5f, 7.0f, 11.0f, 16.0f, 24.0f
    };
    float ringAlpha[GLARE_RINGS];
    float ringBloom[GLARE_RINGS];
    float colour[3];
    float hdrGain;
    float strength;
    float centreAlpha;
    float centreBloom;
    float *out = g_afGlareVertices;
    int segment;
    int ring;
    int channel;

    if (!g_bGlareActive || viewport == 0)
        return;
    {
        float target = 1.0f - 0.9f * GetGlareCover();
        Uint32 now = SDL_GetTicks();
        float seconds = (float)(now - g_uGlareTicks) / 1000.0f;

        /* After a pause, start where the eclipse is. */
        if (g_uGlareTicks == 0 || seconds > 0.5f)
            g_fGlareStrength = target;
        else
            g_fGlareStrength += (target - g_fGlareStrength) *
                                (1.0f - expf(-seconds / GLARE_EASE_SECONDS));
        g_uGlareTicks = now;
        strength = g_fGlareStrength;
    }
    /* The haze in the light's own colour, lifted towards white. */
    for (channel = 0; channel < 3; channel++) {
        colour[channel] = g_afGlareColour[channel] * 1.15f + 0.08f;
        if (colour[channel] > 1.0f)
            colour[channel] = 1.0f;
    }
    centreAlpha = GetGlareOpacity(0.0f) * strength;
    for (ring = 0; ring < GLARE_RINGS; ring++)
        ringAlpha[ring] = GetGlareOpacity(ringRadius[ring]) * strength;
    ringAlpha[GLARE_RINGS - 1] = 0.0f;
    hdrGain = SdlFineBackdropEnabled() ? SdlHdrGain() : 0.0f;
    for (ring = 0; ring < GLARE_RINGS; ring++)
        ringBloom[ring] = hdrGain * strength * GetGlareBloom(ringRadius[ring]);
    ringBloom[GLARE_RINGS - 1] = 0.0f;
    centreBloom = hdrGain * strength * GetGlareBloom(0.0f);

    for (segment = 0; segment < GLARE_SEGMENTS; segment++) {
        float angle0 = 6.28318531f * (float)segment / GLARE_SEGMENTS;
        float angle1 = 6.28318531f * (float)(segment + 1) / GLARE_SEGMENTS;
        float cos0 = cosf(angle0) * g_fGlareRadius;
        float sin0 = sinf(angle0) * g_fGlareRadius;
        float cos1 = cosf(angle1) * g_fGlareRadius;
        float sin1 = sinf(angle1) * g_fGlareRadius;
        float x = g_fGlareX;
        float y = g_fGlareY;

        out = AppendGlareVertex(out, x, y, colour, centreAlpha, centreBloom);
        out = AppendGlareVertex(out, x + cos0 * ringRadius[0],
                                y + sin0 * ringRadius[0], colour,
                                ringAlpha[0], ringBloom[0]);
        out = AppendGlareVertex(out, x + cos1 * ringRadius[0],
                                y + sin1 * ringRadius[0], colour,
                                ringAlpha[0], ringBloom[0]);
        for (ring = 0; ring + 1 < GLARE_RINGS; ring++) {
            float inner = ringRadius[ring];
            float outer = ringRadius[ring + 1];

            out = AppendGlareVertex(out, x + cos0 * inner, y + sin0 * inner,
                                    colour, ringAlpha[ring], ringBloom[ring]);
            out = AppendGlareVertex(out, x + cos0 * outer, y + sin0 * outer,
                                    colour, ringAlpha[ring + 1],
                                    ringBloom[ring + 1]);
            out = AppendGlareVertex(out, x + cos1 * inner, y + sin1 * inner,
                                    colour, ringAlpha[ring], ringBloom[ring]);
            out = AppendGlareVertex(out, x + cos1 * inner, y + sin1 * inner,
                                    colour, ringAlpha[ring], ringBloom[ring]);
            out = AppendGlareVertex(out, x + cos0 * outer, y + sin0 * outer,
                                    colour, ringAlpha[ring + 1],
                                    ringBloom[ring + 1]);
            out = AppendGlareVertex(out, x + cos1 * outer, y + sin1 * outer,
                                    colour, ringAlpha[ring + 1],
                                    ringBloom[ring + 1]);
        }
    }
    if (SdlGlRendererRecordColourOverlay(
            viewport, g_afGlareVertices,
            (int)((out - g_afGlareVertices) / GLARE_FLOATS)) &&
        hdrGain > 0.0f)
        SdlGlRendererSetLastHdrGain(hdrGain);
}
