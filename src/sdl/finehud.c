/* Fork-only: --fine-hud redraws the flight HUD's marks as display-resolution
 * lines instead of scaled-up 320x200 pixels.
 *
 * The marks are frames of the cockpit's TGTLOCK shape, each a one-pixel
 * stroke in one palette entry:
 *   frame 0  the gun crosshair at the view centre: a ring of radius 9 with
 *            an arm on each side running from 6 to 13 out, crossed by ticks
 *            5, 3, 5 and 3 pixels wide at 13, 11, 9 and 7;
 *   frame 1  the missile lock marker: a ring of radius 8, spokes out to 11
 *            above and below and 12 to the sides, and short dashes at 2 to
 *            3 inside;
 *   frame 3  the nav pointer: arms from 2 (3 to the sides) out to 6, each
 *            capped by a bar 3 pixels wide, round a centre dot.
 * Frame 2, the nav marker on the radar scanner, stays a sprite among the
 * scanner's one-pixel dots.  The target brackets, drawn with lines rather
 * than a shape, are redrawn too.
 *
 * The geometry below was traced from those frames, in frame pixels about
 * the shape's origin, so the marks keep their size, place and the 320x200
 * frame's aspect.  Each run of pixels becomes a line over the same extent.
 * Colours are palette entries read through the live palette, so they follow
 * fades.  A frame whose size differs from the traced one keeps its sprite. */
#include "wc1.h"

#include "video_internal.h"

/* Half the stroke width, in frame pixels: thinner than the original's one
 * pixel, but never under one display pixel. */
#define HUD_HALF_WIDTH 0.3f

/* A run of pixels from (x0, y0) to (x1, y1), pixel centres, inclusive. */
typedef struct HudRun {
    signed char x0;
    signed char y0;
    signed char x1;
    signed char y1;
} HudRun;

typedef struct HudMark {
    short width;
    short height;
    short originX;
    short originY;
    float ringRadius;   /* 0 for none */
    int dot;            /* a dot at the origin */
    const HudRun *runs;
    int runCount;
} HudMark;

/* One quarter of each mark, pointing up; the others are it turned. */
static const HudRun g_astCrosshairRuns[] = {
    { 0, -13, 0, -6 },
    { -2, -13, 2, -13 },
    { -1, -11, 1, -11 },
    { -2, -9, 2, -9 },
    { -1, -7, 1, -7 },
};

/* The lock marker's arms are longer to the sides, so it is traced whole. */
static const HudRun g_astLockMarkerRuns[] = {
    { 0, -11, 0, -7 },
    { 0, 7, 0, 11 },
    { -12, 0, -7, 0 },
    { 7, 0, 12, 0 },
    { 0, -3, 0, -2 },
    { 0, 2, 0, 3 },
    { -3, 0, -2, 0 },
    { 2, 0, 3, 0 },
};

static const HudRun g_astNavPointerRuns[] = {
    { 0, -6, 0, -2 },
    { -1, -6, 1, -6 },
    { 0, 2, 0, 6 },
    { -1, 6, 1, 6 },
    { -6, 0, -3, 0 },
    { -6, -1, -6, 1 },
    { 3, 0, 6, 0 },
    { 6, -1, 6, 1 },
};

#define HUD_RUN_COUNT(runs) ((int)(sizeof(runs) / sizeof((runs)[0])))

static const HudMark g_astHudMarks[4] = {
    { 27, 27, 13, 13, 9.0f, 0, g_astCrosshairRuns,
      HUD_RUN_COUNT(g_astCrosshairRuns) },
    { 25, 23, 12, 11, 8.0f, 0, g_astLockMarkerRuns,
      HUD_RUN_COUNT(g_astLockMarkerRuns) },
    { 0, 0, 0, 0, 0.0f, 0, 0, 0 },
    { 13, 13, 6, 6, 0.0f, 1, g_astNavPointerRuns,
      HUD_RUN_COUNT(g_astNavPointerRuns) },
};

/* The crosshair's quarter is drawn four times. */
static int HudMarkTurns(short frame)
{
    return frame == 0 ? 4 : 1;
}

typedef struct HudMarkColour {
    struct Shape *shape;
    int checked;
    int usable;
    unsigned char colour;
} HudMarkColour;

static int g_bFineHud;
static HudMarkColour g_astHudMarkColours[4];

void SdlFineHudEnable(void)
{
    g_bFineHud = 1;
}

/* Nonzero when the marks can be drawn as lines this frame. */
int SdlFineHudActive(void)
{
    return g_bFineHud && SdlUsingGlRenderer() && !SdlEgaDitherEnabled() &&
           !SdlSwcFlightActive();
}

/* Checks a TGTLOCK frame against its traced size and finds its colour. */
static const HudMarkColour *GetHudMarkColour(struct Shape *shape,
                                             short frame)
{
    HudMarkColour *entry = &g_astHudMarkColours[frame];
    const HudMark *mark = &g_astHudMarks[frame];
    unsigned char pixels[27 * 27];
    short width = 0;
    short height = 0;
    short originX = 0;
    short originY = 0;
    int cell;

    if (entry->checked && entry->shape == shape)
        return entry;
    entry->shape = shape;
    entry->checked = 1;
    entry->usable = 0;
    if (mark->runs == 0 || frame >= GetShapeFrameCount(shape))
        return entry;
    GetShapeFrameExtents(shape, frame, &width, &height, &originX, &originY);
    if (width != mark->width || height != mark->height ||
        originX != mark->originX || originY != mark->originY ||
        (size_t)width * height > sizeof(pixels))
        return entry;
    memset(pixels, 0xff, sizeof(pixels));
    DecodeShapeFrame(shape, frame, pixels, width, height, originX, originY);
    for (cell = 0; cell < width * height; cell++) {
        if (pixels[cell] != 0xff) {
            entry->colour = pixels[cell];
            entry->usable = 1;
            break;
        }
    }
    return entry;
}

/* Draws TGTLOCK frame 0, 1 or 3 about (x, y) as lines, in colour, or in the
 * frame's own colour when colour is negative.  Returns 0, having drawn
 * nothing, when the caller should draw the sprite. */
int SdlFineHudDrawMark(const struct Viewport *viewport, float x, float y,
                       struct Shape *shape, short frame, short colour)
{
    const HudMarkColour *entry;
    const HudMark *mark;
    unsigned char drawColour;
    int turn;
    int run;

    if (!SdlFineHudActive() || shape == 0 || shape != pTgtLockShapePtr ||
        frame < 0 || frame > 3)
        return 0;
    entry = GetHudMarkColour(shape, frame);
    if (!entry->usable)
        return 0;
    mark = &g_astHudMarks[frame];
    drawColour = colour < 0 ? entry->colour : (unsigned char)colour;
    if (mark->ringRadius > 0.0f &&
        !SdlGlRendererRecordHudRing(viewport, x, y, mark->ringRadius,
                                    HUD_HALF_WIDTH, drawColour))
        return 0;
    if (mark->dot)
        SdlGlRendererRecordHudRing(viewport, x, y, 0.0f, 0.5f, drawColour);
    for (turn = 0; turn < HudMarkTurns(frame); turn++) {
        for (run = 0; run < mark->runCount; run++) {
            const HudRun *source = &mark->runs[run];
            float x0 = source->x0;
            float y0 = source->y0;
            float x1 = source->x1;
            float y1 = source->y1;
            float extend = 0.5f - HUD_HALF_WIDTH;
            float swap;
            int step;

            /* Turn a quarter at a time: (x, y) to (-y, x). */
            for (step = 0; step < turn; step++) {
                swap = x0;
                x0 = -y0;
                y0 = swap;
                swap = x1;
                x1 = -y1;
                y1 = swap;
            }
            /* The line covers the run's pixels end to end. */
            if (x0 == x1) {
                extend = y0 < y1 ? extend : -extend;
                y0 -= extend;
                y1 += extend;
            } else {
                extend = x0 < x1 ? extend : -extend;
                x0 -= extend;
                x1 += extend;
            }
            SdlGlRendererRecordHudLine(viewport, x + x0, y + y0, x + x1,
                                       y + y1, HUD_HALF_WIDTH, drawColour);
        }
    }
    return 1;
}

/* Records a line over the pixels DrawViewportLine would draw between two
 * points on one row or column. */
static int RecordHudBoxLine(const struct Viewport *viewport, short x0,
                            short y0, short x1, short y1,
                            unsigned char colour)
{
    float extend = 0.5f - HUD_HALF_WIDTH;
    float left = x0 < x1 ? x0 : x1;
    float right = x0 < x1 ? x1 : x0;
    float top = y0 < y1 ? y0 : y1;
    float bottom = y0 < y1 ? y1 : y0;

    if (y0 == y1) {
        left -= extend;
        right += extend;
    } else {
        top -= extend;
        bottom += extend;
    }
    return SdlGlRendererRecordHudLine(viewport, left, top, right, bottom,
                                      HUD_HALF_WIDTH, colour);
}

/* Draws draw_target_box's brackets as lines: the whole box when solid,
 * otherwise its corners, each arm a sixth of the side plus one pixel.
 * Returns 0, having drawn nothing, when the caller should draw them. */
int SdlFineHudDrawBox(const struct Viewport *viewport, short left, short top,
                      short right, short bottom, int solid,
                      unsigned char colour)
{
    short across;
    short down;

    if (!SdlFineHudActive())
        return 0;
    if (solid) {
        if (!RecordHudBoxLine(viewport, left, top, right, top, colour))
            return 0;
        RecordHudBoxLine(viewport, left, bottom, right, bottom, colour);
        RecordHudBoxLine(viewport, left, top, left, bottom, colour);
        RecordHudBoxLine(viewport, right, top, right, bottom, colour);
        return 1;
    }
    across = (short)((right - left) / 6 + 1);
    down = (short)((bottom - top) / 6 + 1);
    if (!RecordHudBoxLine(viewport, left, top, (short)(left + across), top,
                          colour))
        return 0;
    RecordHudBoxLine(viewport, left, bottom, (short)(left + across), bottom,
                     colour);
    RecordHudBoxLine(viewport, right, top, (short)(right - across), top,
                     colour);
    RecordHudBoxLine(viewport, right, bottom, (short)(right - across), bottom,
                     colour);
    RecordHudBoxLine(viewport, left, top, left, (short)(top + down), colour);
    RecordHudBoxLine(viewport, left, bottom, left, (short)(bottom - down),
                     colour);
    RecordHudBoxLine(viewport, right, top, right, (short)(top + down),
                     colour);
    RecordHudBoxLine(viewport, right, bottom, right, (short)(bottom - down),
                     colour);
    return 1;
}
