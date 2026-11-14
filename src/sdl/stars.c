/* Fork-only: --fine-stars draws the backdrop stars as points of light at
 * output resolution.
 *
 * The original's seven stars are frames 32 and up of the constellation
 * shape: a white pixel with a cross of fading arms, a picture of a bright
 * pinprick with glare drawn at 320x200.  Scaled up beside the
 * output-resolution ships they read as blocky crosses.
 *
 * Here each star is redrawn from the same ingredients at the display's own
 * resolution: a core a pixel or so across in the colour of the sprite's
 * centre, a soft halo in the colour of its arms, and for the stars whose
 * arms reach two pixels or more, four thin glare spikes along the screen
 * axes, fading towards their tips.  The spikes stay square to the screen as
 * the original crosses do, which is how glare from a camera's optics
 * behaves.  The halo and spikes are sized from the sprite's arms, read as
 * an angle on the sky: a sprite pixel at the original 90-degree view spans
 * 1/160 of a radian at the centre of the screen, and the glare keeps that
 * span under --fov, growing and shrinking with the focal length as the
 * ships do.  Being counted in game pixels, it also keeps its footprint at
 * any window size.  The core and the spikes' width are counted in display
 * pixels, so they stay sharp.
 *
 * Everything is read from the star's sprite frame, so it follows the data:
 * which palette entries make up the core and the arms, and how far the arms
 * reach.  The colours are looked up in the current palette every frame, so
 * stars follow palette fades.
 *
 * The stars go to the GL object layer as translucent triangles in their
 * place in the painter order.  Without --enhanced, or when the layer is not
 * recording (scenes outside flight), the original sprite is drawn. */
#include "wc1.h"

#include "video_internal.h"

#define STAR_LOOK_CAPACITY 16
#define STAR_SPRITE_PIXELS 1024
/* Sizes in display pixels. */
#define STAR_CORE_RADIUS 1.4f
#define STAR_SPIKE_WIDTH 1.1f
/* Sizes in game pixels per pixel of the sprite's arms. */
#define STAR_HALO_PER_ARM 0.9f
#define STAR_SPIKE_PER_ARM 1.7f
/* Opacity at the centre of the halo and at the root of a spike. */
#define STAR_HALO_ALPHA 0.5f
#define STAR_SPIKE_ALPHA 0.8f
#define STAR_MIN_SPIKE_ARM 2
#define STAR_HALO_SEGMENTS 12
#define STAR_CORE_SEGMENTS 8
#define STAR_FLOATS 7
/* Halo: 12 centre triangles and two rings of 12 quads.  Core: 8 triangles.
 * Spikes: four of a quad and a triangle. */
#define STAR_MAX_VERTICES                                                \
    ((STAR_HALO_SEGMENTS * 5 + STAR_CORE_SEGMENTS + 4 * 3) * 3)

typedef struct StarLook {
    struct Shape *shape;
    short frame;
    /* Palette entries: the centre, the arm pixel next to it, and the arm's
     * last pixel. */
    int core;
    int glare;
    int tip;
    /* How far the longest arm reaches from the centre, in sprite pixels. */
    int arm;
} StarLook;

static int g_bFineStarsEnabled;
static StarLook g_aStarLooks[STAR_LOOK_CAPACITY];
static int g_nStarLookCount;
static float g_afStarVertices[STAR_MAX_VERTICES * STAR_FLOATS];

void SdlFineStarsEnable(void)
{
    g_bFineStarsEnabled = 1;
}

static int StarLuma(const unsigned char *quads, int entry)
{
    return quads[entry * 4 + 2] * 2 + quads[entry * 4 + 1] * 5 +
           quads[entry * 4];
}

/* Reads the parts of a star out of its sprite frame. */
static int ReadStarLook(struct Shape *shape, short frame,
                        const unsigned char *quads, StarLook *look)
{
    static const int stepX[4] = { 1, -1, 0, 0 };
    static const int stepY[4] = { 0, 0, 1, -1 };
    static unsigned char pixels[STAR_SPRITE_PIXELS];
    short width = 0;
    short height = 0;
    short originX = 0;
    short originY = 0;
    int direction;
    int count;
    int index;

    if (frame < 0 || frame >= GetShapeFrameCount(shape))
        return 0;
    GetShapeFrameExtents(shape, frame, &width, &height, &originX, &originY);
    if (width <= 0 || height <= 0 ||
        (int)width * height > STAR_SPRITE_PIXELS)
        return 0;
    count = (int)width * height;
    SDL_memset(pixels, 0xff, (size_t)count);
    DecodeShapeFrame(shape, frame, pixels, width, height, originX, originY);

    look->shape = shape;
    look->frame = frame;
    look->arm = 0;
    look->core = -1;
    if (originX >= 0 && originX < width && originY >= 0 && originY < height)
        look->core = pixels[originY * width + originX];
    if (look->core == 0xff || look->core < 0) {
        /* No pixel on the origin: take the brightest one. */
        look->core = -1;
        for (index = 0; index < count; index++) {
            if (pixels[index] != 0xff &&
                (look->core < 0 ||
                 StarLuma(quads, pixels[index]) > StarLuma(quads, look->core)))
                look->core = pixels[index];
        }
        if (look->core < 0)
            return 0;
        look->glare = look->core;
        look->tip = look->core;
        return 1;
    }
    look->glare = look->core;
    look->tip = look->core;
    for (direction = 0; direction < 4; direction++) {
        int x = originX + stepX[direction];
        int y = originY + stepY[direction];
        int reach = 0;

        while (x >= 0 && x < width && y >= 0 && y < height &&
               pixels[y * width + x] != 0xff) {
            reach++;
            if (reach == 1 && look->arm == 0)
                look->glare = pixels[y * width + x];
            if (reach > look->arm) {
                look->arm = reach;
                look->tip = pixels[y * width + x];
            }
            x += stepX[direction];
            y += stepY[direction];
        }
    }
    return 1;
}

static const StarLook *GetStarLook(struct Shape *shape, short frame,
                                   const unsigned char *quads)
{
    StarLook *look;
    int index;

    for (index = 0; index < g_nStarLookCount; index++) {
        if (g_aStarLooks[index].shape == shape &&
            g_aStarLooks[index].frame == frame)
            return &g_aStarLooks[index];
    }
    /* A new constellation shape is a new set of frames. */
    if (g_nStarLookCount > 0 && g_aStarLooks[0].shape != shape)
        g_nStarLookCount = 0;
    if (g_nStarLookCount >= STAR_LOOK_CAPACITY)
        g_nStarLookCount = 0;
    look = &g_aStarLooks[g_nStarLookCount];
    if (!ReadStarLook(shape, frame, quads, look))
        return 0;
    g_nStarLookCount++;
    return look;
}

static void GetStarColour(const unsigned char *quads, int entry,
                          float colour[3])
{
    colour[0] = quads[entry * 4 + 2] / 255.0f;
    colour[1] = quads[entry * 4 + 1] / 255.0f;
    colour[2] = quads[entry * 4] / 255.0f;
}

static float *AppendStarVertex(float *out, float x, float y,
                               const float colour[3], float alpha)
{
    out[0] = x;
    out[1] = y;
    out[2] = 0.0f;
    out[3] = colour[0];
    out[4] = colour[1];
    out[5] = colour[2];
    out[6] = alpha;
    return out + STAR_FLOATS;
}

/* A disc whose opacity falls from the centre through the given rings to
 * nothing at the rim.  ringRadius and ringAlpha hold rings + 1 entries, the
 * last being the rim. */
static float *AppendStarDisc(float *out, float x, float y, int segments,
                             int rings, const float *ringRadius,
                             const float *ringAlpha, const float colour[3],
                             float centreAlpha)
{
    int segment;
    int ring;

    for (segment = 0; segment < segments; segment++) {
        float angle0 = 6.28318531f * (float)segment / (float)segments;
        float angle1 = 6.28318531f * (float)(segment + 1) / (float)segments;
        float cos0 = SDL_cosf(angle0);
        float sin0 = SDL_sinf(angle0);
        float cos1 = SDL_cosf(angle1);
        float sin1 = SDL_sinf(angle1);

        out = AppendStarVertex(out, x, y, colour, centreAlpha);
        out = AppendStarVertex(out, x + cos0 * ringRadius[0],
                               y + sin0 * ringRadius[0], colour, ringAlpha[0]);
        out = AppendStarVertex(out, x + cos1 * ringRadius[0],
                               y + sin1 * ringRadius[0], colour, ringAlpha[0]);
        for (ring = 0; ring < rings; ring++) {
            float inner = ringRadius[ring];
            float outer = ringRadius[ring + 1];

            out = AppendStarVertex(out, x + cos0 * inner, y + sin0 * inner,
                                   colour, ringAlpha[ring]);
            out = AppendStarVertex(out, x + cos0 * outer, y + sin0 * outer,
                                   colour, ringAlpha[ring + 1]);
            out = AppendStarVertex(out, x + cos1 * inner, y + sin1 * inner,
                                   colour, ringAlpha[ring]);
            out = AppendStarVertex(out, x + cos1 * inner, y + sin1 * inner,
                                   colour, ringAlpha[ring]);
            out = AppendStarVertex(out, x + cos0 * outer, y + sin0 * outer,
                                   colour, ringAlpha[ring + 1]);
            out = AppendStarVertex(out, x + cos1 * outer, y + sin1 * outer,
                                   colour, ringAlpha[ring + 1]);
        }
    }
    return out;
}

/* One glare spike from the centre along (dirX, dirY): it narrows and fades
 * quickly over its first third, then tapers to nothing at the tip. */
static float *AppendStarSpike(float *out, float x, float y, float dirX,
                              float dirY, float length, float width,
                              const float root[3], const float tip[3],
                              float alpha)
{
    float bend = length * 0.35f;
    float rootHalf = width * 0.5f;
    float bendHalf = width * 0.3f;
    float bendColour[3];
    float bendAlpha = alpha * 0.3f;
    float bendX = x + dirX * bend;
    float bendY = y + dirY * bend;
    float tipX = x + dirX * length;
    float tipY = y + dirY * length;
    int channel;

    for (channel = 0; channel < 3; channel++)
        bendColour[channel] =
            root[channel] + (tip[channel] - root[channel]) * 0.35f;
    out = AppendStarVertex(out, x - dirY * rootHalf, y + dirX * rootHalf,
                           root, alpha);
    out = AppendStarVertex(out, x + dirY * rootHalf, y - dirX * rootHalf,
                           root, alpha);
    out = AppendStarVertex(out, bendX - dirY * bendHalf,
                           bendY + dirX * bendHalf, bendColour, bendAlpha);
    out = AppendStarVertex(out, bendX - dirY * bendHalf,
                           bendY + dirX * bendHalf, bendColour, bendAlpha);
    out = AppendStarVertex(out, x + dirY * rootHalf, y - dirX * rootHalf,
                           root, alpha);
    out = AppendStarVertex(out, bendX + dirY * bendHalf,
                           bendY - dirX * bendHalf, bendColour, bendAlpha);
    out = AppendStarVertex(out, bendX - dirY * bendHalf,
                           bendY + dirX * bendHalf, bendColour, bendAlpha);
    out = AppendStarVertex(out, bendX + dirY * bendHalf,
                           bendY - dirX * bendHalf, bendColour, bendAlpha);
    out = AppendStarVertex(out, tipX, tipY, tip, 0.0f);
    return out;
}

/* Records star obj, at (x, y) in the space frame, in the GL object layer.
 * Returns zero if the caller must draw the original sprite. */
int SdlFineStarDraw(const Viewport *viewport, short obj, float x, float y,
                    struct Shape *shape, short frame)
{
    static const float spikeX[4] = { 1.0f, -1.0f, 0.0f, 0.0f };
    static const float spikeY[4] = { 0.0f, 0.0f, 1.0f, -1.0f };
    const unsigned char *quads;
    const StarLook *look;
    float coreColour[3];
    float glareColour[3];
    float tipColour[3];
    float haloRadius[4];
    float haloAlpha[4];
    float coreRadius[1];
    float coreAlpha[1];
    float pixel;
    float brightness;
    float reach;
    float *out;
    int spike;

    if (!g_bFineStarsEnabled || SdlSwcFlightActive() || viewport == 0 ||
        shape == 0 || aeObjectClass[obj] != OBJECT_CLASS_STAR)
        return 0;
    if (!SdlUsingGlRenderer() || SdlEgaDitherEnabled())
        return 0;
    /* Point-cloud ships composed into the indexed frame would be drawn
     * under the object layer; keep everything in that frame then. */
    if (SdlShip3dEnabled() && !SdlShip3dUsesGl())
        return 0;
    quads = SdlGetPaletteQuads();
    look = GetStarLook(shape, frame, quads);
    if (look == 0)
        return 0;

    pixel = SdlGlRendererOutputScale();
    if (pixel < 1.0f)
        pixel = 1.0f;
    pixel = 1.0f / pixel;
    GetStarColour(quads, look->core, coreColour);
    GetStarColour(quads, look->glare, glareColour);
    GetStarColour(quads, look->tip, tipColour);
    brightness = StarLuma(quads, look->core) / (255.0f * 8.0f);
    /* The sprite's arms as an arc on the sky, at the focal length in use:
     * a wider --fov puts more sky into each game pixel. */
    reach = ((float)look->arm + 0.5f) * SdlFovFocal() /
            ((float)(nScreenWidth & ~1) * 0.5f);
    out = g_afStarVertices;

    /* The halo: a quick fall from the centre, then a long faint skirt. */
    haloRadius[0] = reach * STAR_HALO_PER_ARM * 0.2f;
    haloRadius[1] = reach * STAR_HALO_PER_ARM * 0.5f;
    haloRadius[2] = reach * STAR_HALO_PER_ARM;
    haloAlpha[0] = STAR_HALO_ALPHA * brightness * 0.45f;
    haloAlpha[1] = STAR_HALO_ALPHA * brightness * 0.12f;
    haloAlpha[2] = 0.0f;
    out = AppendStarDisc(out, x, y, STAR_HALO_SEGMENTS, 2, haloRadius,
                         haloAlpha, glareColour, STAR_HALO_ALPHA * brightness);

    if (look->arm >= STAR_MIN_SPIKE_ARM) {
        for (spike = 0; spike < 4; spike++)
            out = AppendStarSpike(out, x, y, spikeX[spike], spikeY[spike],
                                  reach * STAR_SPIKE_PER_ARM,
                                  STAR_SPIKE_WIDTH * pixel, glareColour,
                                  tipColour, STAR_SPIKE_ALPHA * brightness);
    }

    /* The core last, over the glare. */
    coreRadius[0] = STAR_CORE_RADIUS * pixel;
    if (coreRadius[0] > 0.75f)
        coreRadius[0] = 0.75f;
    coreAlpha[0] = 0.0f;
    out = AppendStarDisc(out, x, y, STAR_CORE_SEGMENTS, 0, coreRadius,
                         coreAlpha, coreColour, 1.0f);

    return SdlGlRendererRecordColourOverlay(
        viewport, g_afStarVertices,
        (int)((out - g_afStarVertices) / STAR_FLOATS));
}
