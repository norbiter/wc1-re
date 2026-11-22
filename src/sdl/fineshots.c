/* Fork-only: --fine-shots redraws the gun shots at output resolution.
 *
 * Laser, mass driver and neutron bolts (and turret bolts, which borrow the
 * laser's) are each one OBJECTS.VGA frame of 8 to 17 game pixels: a ball of
 * concentric palette bands round a bright core.  The game scales that frame
 * with distance, up to 32 times, so a bolt passing close fills the view with
 * blocky rings.
 *
 * Each shot is rebuilt from its own frame by smoothing it in polar
 * coordinates about its brightness-weighted centre: narrowly across radius,
 * so the stepped bands become one even gradient, and more widely round it,
 * so the dither within a band evens out while the shape (the mass driver's
 * tail, for one) is kept.  The outline is smoothed the same way, which
 * gives it a soft edge a fraction of a game pixel wide.
 *
 * Laser and neutron bolts are also drawn as bolts: the ball, turned to face
 * along the shot's path on screen, sits at the shot's true position, and a
 * trail runs back from it to where the shot was SHOT_TRAIL_TICKS sim ticks
 * earlier.  The trail is the ball swept along that path: across it, each
 * line takes the brightest pixel of the ball's matching row, so the hot core
 * runs back as a line inside the ball's colours.  It runs from the ball's
 * centre back to the tail, fades out towards the tail, and is drawn under
 * the ball.  Its far end is projected
 * from the shot's position less its velocity times those ticks, so the
 * trail is a fixed length in space, foreshortened with perspective, and
 * the same length at any frame rate.  A newly fired bolt's trail grows out
 * of the gun, never reaching back past where it was fired.  Under --smooth
 * the shot's position is already advanced for each extra frame, and the
 * trail's growth takes in the fraction of a tick too.
 *
 * Each shape gets one image, built on the CPU the first time a shot of it
 * is drawn, at SHOT_IMAGE_SCALE display pixels per game pixel; the GL layer
 * draws it at whatever size the shot has, mipmapped below that size and
 * magnified above it.  The polar smoothing, the costly part, is worked out
 * at SHOT_FIELD_SCALE samples per game pixel only, and its colour and
 * coverage are interpolated up from there.  Building an image at each size
 * a receding shot passes through stalled the frame for tens of
 * milliseconds per size.  Images are built from the palette of that moment
 * and a tint keeps them in step with palette fades.  Without --enhanced the
 * sprites are drawn as before. */
#include "wc1.h"

#include <math.h>

#include "video_internal.h"

#define SHOT_PAD 3
#define SHOT_MAX_SIDE 32
#define SHOT_MAX_CELLS (SHOT_MAX_SIDE * SHOT_MAX_SIDE)
/* One entry per shape: the laser (turrets share it), mass driver and
 * neutron gun, with a spare. */
#define SHOT_CACHE_CAPACITY 4
#define SHOT_TRANSPARENT 0xff
/* Display pixels per game pixel the images are built at, and smoothing
 * samples per game pixel. */
#define SHOT_IMAGE_SCALE 12
#define SHOT_FIELD_SCALE 3
#define SHOT_MAX_FIELD (SHOT_MAX_SIDE * SHOT_FIELD_SCALE)

/* Kernel widths in game pixels: across radius, and along the circle for
 * colour and for the outline.  The outline is smoothed further round, which
 * takes the stair steps out of the sprite's pixel circle. */
#define SHOT_SIGMA_RADIAL 0.55f
#define SHOT_SIGMA_TANGENTIAL 1.25f
#define SHOT_SIGMA_OUTLINE 2.5f
/* How far back a bolt's trail reaches, in sim ticks of its movement. */
#define SHOT_TRAIL_TICKS 3.0
/* The trail image's length in texels; it is all one profile but for its
 * fade, which bilinear filtering stretches smoothly. */
#define SHOT_TRAIL_TEXELS 64
/* Where the trail fades in from its tail, as a share of its length,
 * and the nearest a trail's tail may come to the eye, as a share of its
 * head's depth. */
#define SHOT_TRAIL_FADE_START 0.05f
#define SHOT_TRAIL_FADE_END 0.9f
#define SHOT_TRAIL_NEAR 0.1
/* Display pixels the antialiased outline takes to fade. */
#define SHOT_EDGE_PIXELS 1.5f

typedef struct ShotImage {
    struct Shape *shape;
    unsigned int generation;
    /* The ball, and the ball swept into a trail faded from its left edge.
     * 0 with failed set: this frame cannot be treated; draw the sprite. */
    int ballHandle;
    int trailHandle;
    int failed;
    short spriteWidth;
    short spriteHeight;
    short originX;
    short originY;
    /* How many pixels use each palette entry, and the colour they summed to
     * when the image was built. */
    unsigned short entryCount[256];
    float builtColour[3];
    unsigned int lastUse;
} ShotImage;

/* The smoothed sprite sampled SHOT_FIELD_SCALE times per game pixel:
 * colour (0..1) and how much of the neighbourhood is covered. */
typedef struct ShotField {
    int width;
    int height;
    float colour[3][SHOT_MAX_FIELD * SHOT_MAX_FIELD];
    float coverage[SHOT_MAX_FIELD * SHOT_MAX_FIELD];
} ShotField;

static int g_bFineShotsEnabled;
static ShotImage g_aShotImages[SHOT_CACHE_CAPACITY];
static unsigned int g_uShotUse;
static unsigned char g_abShotPixels[SHOT_MAX_CELLS];
static ShotField g_stShotField;

void SdlFineShotsEnable(void)
{
    g_bFineShotsEnabled = 1;
}

static float ClampShot(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static void SumShotColour(const ShotImage *image, const unsigned char *quads,
                          float sum[3])
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

/* The sprite's cells in polar form about its centre. */
typedef struct ShotCell {
    float radius;
    float angle;
    float colour[3];
} ShotCell;

static ShotCell g_aShotCells[SHOT_MAX_CELLS];
static ShotCell g_aShotEmpty[SHOT_MAX_CELLS];

/* Smooths the sprite in polar coordinates into the field.  Every grid cell,
 * the transparent margin included, weighs in on coverage; only opaque cells
 * on colour, so the edge fades without darkening. */
static void SmoothShotField(const unsigned char *pixels, int spriteWidth,
                            int spriteHeight, const unsigned char *quads,
                            ShotField *field)
{
    int gridWidth = spriteWidth + SHOT_PAD * 2;
    int gridHeight = spriteHeight + SHOT_PAD * 2;
    float centreX = 0.0f;
    float centreY = 0.0f;
    float centreWeight = 0.0f;
    float radialFactor = -0.5f / (SHOT_SIGMA_RADIAL * SHOT_SIGMA_RADIAL);
    float tangentialFactor =
        -0.5f / (SHOT_SIGMA_TANGENTIAL * SHOT_SIGMA_TANGENTIAL);
    float outlineFactor = -0.5f / (SHOT_SIGMA_OUTLINE * SHOT_SIGMA_OUTLINE);
    float reach = SHOT_SIGMA_RADIAL * 3.0f;
    int cellCount = 0;
    int emptyCount = 0;
    int x;
    int y;

    /* Weighting by a high power of brightness finds the core, which the
     * bands are drawn round; the frame's origin need not be on it. */
    for (y = 0; y < spriteHeight; y++) {
        for (x = 0; x < spriteWidth; x++) {
            unsigned char entry = pixels[y * spriteWidth + x];
            float luminance;
            float weight;

            if (entry == SHOT_TRANSPARENT)
                continue;
            luminance = (0.2126f * quads[entry * 4 + 2] +
                         0.7152f * quads[entry * 4 + 1] +
                         0.0722f * quads[entry * 4]) /
                            255.0f +
                        0.01f;
            weight = luminance * luminance;
            weight *= weight;
            weight *= weight;
            centreX += weight * ((float)x + 0.5f);
            centreY += weight * ((float)y + 0.5f);
            centreWeight += weight;
        }
    }
    if (centreWeight <= 0.0f) {
        centreX = spriteWidth * 0.5f;
        centreY = spriteHeight * 0.5f;
    } else {
        centreX /= centreWeight;
        centreY /= centreWeight;
    }
    centreX += SHOT_PAD;
    centreY += SHOT_PAD;

    for (y = 0; y < gridHeight; y++) {
        for (x = 0; x < gridWidth; x++) {
            int sx = x - SHOT_PAD;
            int sy = y - SHOT_PAD;
            unsigned char entry = SHOT_TRANSPARENT;
            float dx = (float)x + 0.5f - centreX;
            float dy = (float)y + 0.5f - centreY;
            ShotCell *cell;

            if (sx >= 0 && sy >= 0 && sx < spriteWidth && sy < spriteHeight)
                entry = pixels[sy * spriteWidth + sx];
            cell = entry == SHOT_TRANSPARENT ? &g_aShotEmpty[emptyCount++]
                                             : &g_aShotCells[cellCount++];
            cell->radius = sqrtf(dx * dx + dy * dy);
            cell->angle = atan2f(dy, dx);
            if (entry != SHOT_TRANSPARENT) {
                cell->colour[0] = quads[entry * 4 + 2] / 255.0f;
                cell->colour[1] = quads[entry * 4 + 1] / 255.0f;
                cell->colour[2] = quads[entry * 4] / 255.0f;
            }
        }
    }

    field->width = gridWidth * SHOT_FIELD_SCALE;
    field->height = gridHeight * SHOT_FIELD_SCALE;
    for (y = 0; y < field->height; y++) {
        for (x = 0; x < field->width; x++) {
            float px = ((float)x + 0.5f) / SHOT_FIELD_SCALE - centreX;
            float py = ((float)y + 0.5f) / SHOT_FIELD_SCALE - centreY;
            float radius = sqrtf(px * px + py * py);
            float angle = atan2f(py, px);
            float sum[3] = { 0.0f, 0.0f, 0.0f };
            float solid = 0.0f;
            float covered = 0.0f;
            float total = 0.0f;
            int sample = y * field->width + x;
            int channel;
            int pass;
            int index;

            for (pass = 0; pass < 2; pass++) {
                const ShotCell *cells = pass == 0 ? g_aShotCells
                                                  : g_aShotEmpty;
                int count = pass == 0 ? cellCount : emptyCount;

                for (index = 0; index < count; index++) {
                    const ShotCell *cell = &cells[index];
                    float radial = radius - cell->radius;
                    float turn;
                    float arc;
                    float weight;
                    float outline;

                    if (radial > reach || radial < -reach)
                        continue;
                    turn = angle - cell->angle;
                    if (turn > 3.14159265f)
                        turn -= 6.28318531f;
                    else if (turn < -3.14159265f)
                        turn += 6.28318531f;
                    arc = turn * (radius + cell->radius) * 0.5f;
                    arc *= arc;
                    radial *= radial * radialFactor;
                    outline = expf(radial + arc * outlineFactor);
                    total += outline;
                    if (pass != 0)
                        continue;
                    covered += outline;
                    weight = expf(radial + arc * tangentialFactor);
                    solid += weight;
                    sum[0] += weight * cell->colour[0];
                    sum[1] += weight * cell->colour[1];
                    sum[2] += weight * cell->colour[2];
                }
            }
            field->coverage[sample] = total > 0.0f ? covered / total : 0.0f;
            for (channel = 0; channel < 3; channel++)
                field->colour[channel][sample] =
                    solid > 0.0f ? sum[channel] / solid : 0.0f;
        }
    }
}

static void CatmullRomShotWeights(float t, float weights[4])
{
    float t2 = t * t;
    float t3 = t2 * t;

    weights[0] = 0.5f * (-t3 + 2.0f * t2 - t);
    weights[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
    weights[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
    weights[3] = 0.5f * (t3 - t2);
}

/* Fills a premultiplied RGBA image, scale display pixels per game pixel,
 * from the field.  The outline is where the coverage crosses one half,
 * drawn SHOT_EDGE_PIXELS wide. */
static void RenderShot(const ShotField *field, int scale, unsigned char *out)
{
    int outWidth = field->width / SHOT_FIELD_SCALE * scale;
    int outHeight = field->height / SHOT_FIELD_SCALE * scale;
    /* Coverage falls across the outline at 1 / (sqrt(2 pi) sigma) per game
     * pixel; this gain makes alpha fall over SHOT_EDGE_PIXELS instead. */
    float edgeGain = 2.5066283f * SHOT_SIGMA_RADIAL * (float)scale /
                     SHOT_EDGE_PIXELS;
    int x;
    int y;

    for (y = 0; y < outHeight; y++) {
        float v = ((float)y + 0.5f) * SHOT_FIELD_SCALE / (float)scale - 0.5f;
        int baseY = (int)floorf(v);
        float weightsY[4];

        CatmullRomShotWeights(v - (float)baseY, weightsY);
        for (x = 0; x < outWidth; x++) {
            float u =
                ((float)x + 0.5f) * SHOT_FIELD_SCALE / (float)scale - 0.5f;
            int baseX = (int)floorf(u);
            float weightsX[4];
            float colour[3] = { 0.0f, 0.0f, 0.0f };
            float coverage = 0.0f;
            float alpha;
            unsigned char *pixel = out + ((size_t)y * outWidth + x) * 4U;
            int i;
            int j;
            int channel;

            CatmullRomShotWeights(u - (float)baseX, weightsX);
            for (j = 0; j < 4; j++) {
                int sy = baseY - 1 + j;

                sy = sy < 0 ? 0 : sy >= field->height ? field->height - 1 : sy;
                for (i = 0; i < 4; i++) {
                    int sx = baseX - 1 + i;
                    int sample;
                    float weight = weightsX[i] * weightsY[j];

                    sx = sx < 0 ? 0 : sx >= field->width ? field->width - 1
                                                          : sx;
                    sample = sy * field->width + sx;
                    coverage += weight * field->coverage[sample];
                    for (channel = 0; channel < 3; channel++)
                        colour[channel] +=
                            weight * field->colour[channel][sample];
                }
            }
            alpha = ClampShot((coverage - 0.5f) * edgeGain + 0.5f, 0.0f,
                              1.0f);
            if (alpha < 0.004f) {
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
                continue;
            }
            for (channel = 0; channel < 3; channel++)
                pixel[channel] = (unsigned char)(
                    ClampShot(colour[channel], 0.0f, 1.0f) * alpha * 255.0f +
                    0.5f);
            pixel[3] = (unsigned char)(alpha * 255.0f + 0.5f);
        }
    }
}

/* Sweeps a premultiplied ball image along x into a trail: each row takes
 * the ball row's brightest pixel, faded in from the left edge. */
static void BuildShotTrail(const unsigned char *ball, int ballWidth,
                           int height, unsigned char *out)
{
    float fade[SHOT_TRAIL_TEXELS];
    int x;
    int y;
    int channel;

    for (x = 0; x < SHOT_TRAIL_TEXELS; x++) {
        float t = (((float)x + 0.5f) / (float)SHOT_TRAIL_TEXELS -
                   SHOT_TRAIL_FADE_START) /
                  (SHOT_TRAIL_FADE_END - SHOT_TRAIL_FADE_START);

        t = ClampShot(t, 0.0f, 1.0f);
        fade[x] = t * t * (3.0f - 2.0f * t);
    }
    for (y = 0; y < height; y++) {
        const unsigned char *row = ball + (size_t)y * ballWidth * 4U;
        const unsigned char *brightest = row;
        float best = -1.0f;

        for (x = 0; x < ballWidth; x++) {
            const unsigned char *pixel = row + (size_t)x * 4U;
            float value = 0.2126f * pixel[0] + 0.7152f * pixel[1] +
                          0.0722f * pixel[2] + 0.01f * pixel[3];

            if (value > best) {
                best = value;
                brightest = pixel;
            }
        }
        for (x = 0; x < SHOT_TRAIL_TEXELS; x++) {
            unsigned char *pixel =
                out + ((size_t)y * SHOT_TRAIL_TEXELS + x) * 4U;

            for (channel = 0; channel < 4; channel++)
                pixel[channel] = (unsigned char)(
                    (float)brightest[channel] * fade[x] + 0.5f);
        }
    }
}

static void BuildShotImage(ShotImage *image, struct Shape *shape,
                           const unsigned char *quads)
{
    unsigned char *ball;
    unsigned char *trail;
    short width = 0;
    short height = 0;
    short originX = 0;
    short originY = 0;
    int ballWidth;
    int ballHeight;
    int cell;

    image->failed = 1;
    image->ballHandle = 0;
    image->trailHandle = 0;
    if (GetShapeFrameCount(shape) < 1)
        return;
    GetShapeFrameExtents(shape, 0, &width, &height, &originX, &originY);
    if (width <= 0 || height <= 0 ||
        width + SHOT_PAD * 2 > SHOT_MAX_SIDE ||
        height + SHOT_PAD * 2 > SHOT_MAX_SIDE)
        return;
    memset(g_abShotPixels, SHOT_TRANSPARENT, (size_t)width * (size_t)height);
    DecodeShapeFrame(shape, 0, g_abShotPixels, width, height, originX,
                     originY);
    image->spriteWidth = width;
    image->spriteHeight = height;
    image->originX = originX;
    image->originY = originY;
    memset(image->entryCount, 0, sizeof(image->entryCount));
    for (cell = 0; cell < (int)width * height; cell++) {
        if (g_abShotPixels[cell] != SHOT_TRANSPARENT)
            image->entryCount[g_abShotPixels[cell]]++;
    }
    SumShotColour(image, quads, image->builtColour);
    SmoothShotField(g_abShotPixels, width, height, quads, &g_stShotField);
    ballWidth = (width + SHOT_PAD * 2) * SHOT_IMAGE_SCALE;
    ballHeight = (height + SHOT_PAD * 2) * SHOT_IMAGE_SCALE;
    ball = malloc((size_t)ballWidth * (size_t)ballHeight * 4U);
    trail = malloc((size_t)SHOT_TRAIL_TEXELS * (size_t)ballHeight * 4U);
    if (ball != 0 && trail != 0) {
        RenderShot(&g_stShotField, SHOT_IMAGE_SCALE, ball);
        BuildShotTrail(ball, ballWidth, ballHeight, trail);
        image->ballHandle =
            SdlGlRendererCreateImage(ballWidth, ballHeight, ball);
        image->trailHandle =
            SdlGlRendererCreateImage(SHOT_TRAIL_TEXELS, ballHeight, trail);
        image->failed = image->ballHandle == 0;
    }
    free(ball);
    free(trail);
}

static ShotImage *GetShotImage(struct Shape *shape,
                               const unsigned char *quads)
{
    unsigned int generation = SdlGlRendererImageGeneration();
    ShotImage *image = 0;
    ShotImage *oldest = &g_aShotImages[0];
    int index;

    g_uShotUse++;
    for (index = 0; index < SHOT_CACHE_CAPACITY; index++) {
        ShotImage *entry = &g_aShotImages[index];

        if (entry->generation == generation && entry->shape == shape &&
            (entry->ballHandle != 0 || entry->failed))
            image = entry;
        if (entry->lastUse < oldest->lastUse)
            oldest = entry;
    }
    if (image != 0) {
        float now[3];

        /* Built during a fade: rebuild once the palette is well brighter. */
        SumShotColour(image, quads, now);
        if (image->failed ||
            now[0] + now[1] + now[2] <=
                (image->builtColour[0] + image->builtColour[1] +
                 image->builtColour[2]) * 1.2f + 1.0f) {
            image->lastUse = g_uShotUse;
            return image;
        }
        oldest = image;
    }
    if (oldest->generation == generation) {
        if (oldest->ballHandle != 0)
            SdlGlRendererReleaseImage(oldest->ballHandle);
        if (oldest->trailHandle != 0)
            SdlGlRendererReleaseImage(oldest->trailHandle);
    }
    memset(oldest, 0, sizeof(*oldest));
    oldest->shape = shape;
    oldest->generation = generation;
    oldest->lastUse = g_uShotUse;
    BuildShotImage(oldest, shape, quads);
    return oldest;
}

/* The shots this treats: the gun bolts, whose shape is their type's own (a
 * turret bolt's is the laser's). */
static int IsFineShot(short obj, struct Shape *shape)
{
    int type = aeObjectTypeList[obj];

    if (aeObjectClass[obj] != OBJECT_CLASS_PROJECTILE)
        return 0;
    if (type != OBJECT_TYPE_LASER_CANNON &&
        type != OBJECT_TYPE_NEUTRON_PARTICLE_GUN &&
        type != OBJECT_TYPE_MASS_DRIVER_CANNON &&
        type != OBJECT_TYPE_TURRET)
        return 0;
    return aObjectTypeData[type].shapeSet == shape;
}

/* For a laser or neutron bolt, finds its trail on screen: the unit vector
 * from the trail's tail towards the shot, and its length in frame pixels.
 * Returns 0 for a shot drawn as a plain ball. */
static int GetShotTrail(short obj, float *axisX, float *axisY,
                        float *length)
{
    const FixedVector *view = &aObjectViewPositionsList[obj];
    const FixedVector *velocity = &aShipVelocity[obj];
    const FixedVector *right = &aShipRightVector[EYE_OBJECT];
    const FixedVector *up = &aShipUpVector[EYE_OBJECT];
    const FixedVector *forward = &aShipFwdVec[EYE_OBJECT];
    int type = aeObjectTypeList[obj];
    double ticks;
    double tailX;
    double tailY;
    double tailZ;
    double nearZ;
    double focal;
    double dx;
    double dy;
    double distance;

    if (type != OBJECT_TYPE_LASER_CANNON &&
        type != OBJECT_TYPE_NEUTRON_PARTICLE_GUN)
        return 0;
    if (view->z <= 0)
        return 0;
    /* fire_weapon sets the counter to the lifetime and each tick counts it
     * down, so it tells how long the shot has flown. */
    ticks = (double)(aObjectTypeData[type].lifetime -
                     asObjectCounterArray[obj]) +
            SdlSmoothFraction();
    if (ticks > SHOT_TRAIL_TICKS)
        ticks = SHOT_TRAIL_TICKS;
    if (ticks <= 0.0)
        return 0;
    /* The velocity in the eye's frame, as transform_to_objects_frame turns
     * the shot's position into its view position. */
    tailX = (double)view->x -
            ticks * ((double)velocity->x * right->x +
                     (double)velocity->y * right->y +
                     (double)velocity->z * right->z) / 256.0;
    tailY = (double)view->y -
            ticks * ((double)velocity->x * up->x +
                     (double)velocity->y * up->y +
                     (double)velocity->z * up->z) / 256.0;
    tailZ = (double)view->z -
            ticks * ((double)velocity->x * forward->x +
                     (double)velocity->y * forward->y +
                     (double)velocity->z * forward->z) / 256.0;
    /* A shot flying away from the eye has its tail nearer; keep that end in
     * front of the eye, cut where it would pass too close. */
    nearZ = (double)view->z * SHOT_TRAIL_NEAR;
    if (tailZ < nearZ) {
        double cut = ((double)view->z - nearZ) / ((double)view->z - tailZ);

        tailX = view->x + (tailX - view->x) * cut;
        tailY = view->y + (tailY - view->y) * cut;
        tailZ = nearZ;
    }
    focal = SdlFovFocal();
    dx = focal * ((double)view->x / view->z - tailX / tailZ);
    dy = focal * ((double)view->y / view->z - tailY / tailZ);
    distance = sqrt(dx * dx + dy * dy);
    if (distance < 0.01)
        return 0;
    *axisX = (float)(dx / distance);
    *axisY = (float)(dy / distance);
    *length = (float)distance;
    return 1;
}

/* Records an image quad about the shot's position, its x axis along
 * (axisX, axisY): the ball, or with a trailLength the trail, which runs
 * from that many frame pixels back to the position. */
static int RecordShotQuad(const Viewport *viewport, const ShotImage *image,
                          float x, float y, float spriteScale, float axisX,
                          float axisY, float trailLength,
                          const float tint[3])
{
    float corners[8];
    float left;
    float top;
    float right;
    float bottom;
    int corner;

    /* The sprite's pixel (0, 0) covers -origin - 0.5 to -origin + 0.5 about
     * the shot's position, scaled about it; the image adds a margin. */
    left = (-(float)image->originX - 0.5f - SHOT_PAD) * spriteScale;
    top = (-(float)image->originY - 0.5f - SHOT_PAD) * spriteScale;
    right = left + (float)(image->spriteWidth + SHOT_PAD * 2) * spriteScale;
    bottom = top + (float)(image->spriteHeight + SHOT_PAD * 2) * spriteScale;
    if (trailLength > 0.0f) {
        left = -trailLength;
        right = 0.0f;
    }
    for (corner = 0; corner < 4; corner++) {
        float along = (corner & 2) != 0 ? right : left;
        float across = (corner & 1) != 0 ? bottom : top;

        corners[corner * 2] = x + along * axisX - across * axisY;
        corners[corner * 2 + 1] = y + along * axisY + across * axisX;
    }
    if (!SdlGlRendererRecordImage(
            viewport,
            trailLength > 0.0f ? image->trailHandle : image->ballHandle,
            corners, tint))
        return 0;
    /* --hdr: the bolt's hot core glows above white. */
    SdlGlRendererSetLastHdrGain(SdlHdrGain());
    return 1;
}

int SdlFineShotDraw(const Viewport *viewport, short obj, float x, float y,
                    struct Shape *shape, short frame, short angle,
                    short scale, short flip)
{
    const unsigned char *quads;
    ShotImage *image;
    float spriteScale;
    float now[3];
    float tint[3];
    float axisX;
    float axisY;
    float length;
    int channel;

    if (!g_bFineShotsEnabled || SdlSwcFlightActive() || viewport == 0 ||
        shape == 0 || obj < 0 || obj >= SPACE_OBJECT_COUNT)
        return 0;
    /* Shots are drawn unturned and unflipped from their only frame. */
    if (frame != 0 || angle != 0 || flip != 0 || scale <= 0 ||
        !IsFineShot(obj, shape))
        return 0;
    if (!SdlUsingGlRenderer() || SdlEgaDitherEnabled())
        return 0;
    if (SdlShip3dEnabled() && !SdlShip3dUsesGl())
        return 0;

    spriteScale = (float)(unsigned short)scale / 256.0f;
    quads = SdlGetPaletteQuads();
    image = GetShotImage(shape, quads);
    if (image == 0 || image->failed || image->ballHandle == 0)
        return 0;

    SumShotColour(image, quads, now);
    for (channel = 0; channel < 3; channel++) {
        tint[channel] = image->builtColour[channel] > 0.0f
                            ? now[channel] / image->builtColour[channel]
                            : 1.0f;
        tint[channel] = ClampShot(tint[channel], 0.0f, 1.5f);
    }
    if (!GetShotTrail(obj, &axisX, &axisY, &length))
        return RecordShotQuad(viewport, image, x, y, spriteScale, 1.0f, 0.0f,
                              0.0f, tint);
    if (image->trailHandle != 0)
        RecordShotQuad(viewport, image, x, y, spriteScale, axisX, axisY,
                       length, tint);
    return RecordShotQuad(viewport, image, x, y, spriteScale, axisX, axisY,
                          0.0f, tint);
}
