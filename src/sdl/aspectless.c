/* Fork-only: --aspectless lets cockpit-less space flight fill a fullscreen
 * display of any aspect ratio instead of sitting in a 4:3 box.
 *
 * The game keeps drawing a 320x200 screen.  Only the space buffer grows: it
 * is allocated with a margin on each side, and its row table is laid out so
 * that game coordinates stay those of the 320x200 screen (the margin columns
 * are x < 0 and x > 319, the margin rows y < 0 and y > 199).  While the
 * objects of a frame are drawn the viewport rectangle is widened to cover
 * the margins; the rest of the time it is the usual 0,0-319,199, so the HUD,
 * the mouse and every other user of the rectangle see what they always did.
 * The focal length is untouched, so the middle of the picture is exactly
 * the 4:3 one and the margins are extra field of view.
 *
 * When the frame is dumped to the screen the whole buffer is kept as a
 * backdrop.  At present time the 320x200 screen (with whatever the cockpit
 * code drew over it) is laid over the middle of that backdrop. */
#include "wc1.h"

#include "video_internal.h"

#include <math.h>

#define ASPECTLESS_MAX_MARGIN_X \
    ((SDL_PORT_FRAME_MAX_WIDTH - SDL_PORT_FRAME_WIDTH) / 2)
#define ASPECTLESS_MAX_MARGIN_Y \
    ((SDL_PORT_FRAME_MAX_HEIGHT - SDL_PORT_FRAME_HEIGHT) / 2)
/* The original view cone test: cos(angle off axis) * 256 must reach this. */
#define ASPECTLESS_ORIGINAL_VIEW_CONE 0x94

static int g_bAspectless;
/* The space buffer allocated with margins, or null. */
static unsigned char *g_pWideAllocation;
static int *g_pWideRows;
static int g_nMarginX;
static int g_nMarginY;
/* The viewport rectangle currently covers the margins. */
static int g_bWidened;
/* The backdrop holds the frame last dumped to the screen. */
static int g_bBackdropValid;
static int g_nBackdropMarginX;
static int g_nBackdropMarginY;
static unsigned char g_abBackdrop[SDL_PORT_FRAME_MAX_PIXELS];
static unsigned char g_abComposite[SDL_PORT_FRAME_MAX_PIXELS];

void SdlAspectlessEnable(void)
{
    g_bAspectless = 1;
}

/* The margins that make the frame match the display, in game pixels.  A
 * game pixel is 5:6 (320x200 shown at 4:3), so a frame of W by H has the
 * aspect ratio 5W:6H. */
static void CalculateMargins(int *marginX, int *marginY)
{
    Uint32 flags;
    int height;
    int width;

    *marginX = 0;
    *marginY = 0;
    if (!g_bAspectless || hDIBParentWindow == 0)
        return;
    flags = SDL_GetWindowFlags(hDIBParentWindow);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) ==
        0)
        return;
    SDL_GetWindowSize(hDIBParentWindow, &width, &height);
    if (width < 1 || height < 1)
        return;
    if (width * 3 > height * 4) {
        /* W = 200 * 6/5 * width/height. */
        *marginX = (int)(((double)width * 240.0 / (double)height -
                          SDL_PORT_FRAME_WIDTH) / 2.0 + 0.5);
        if (*marginX > ASPECTLESS_MAX_MARGIN_X)
            *marginX = ASPECTLESS_MAX_MARGIN_X;
    } else if (width * 3 < height * 4) {
        /* H = 320 * 5/6 * height/width. */
        *marginY = (int)(((double)height * 1600.0 / (6.0 * (double)width) -
                          SDL_PORT_FRAME_HEIGHT) / 2.0 + 0.5);
        if (*marginY > ASPECTLESS_MAX_MARGIN_Y)
            *marginY = ASPECTLESS_MAX_MARGIN_Y;
    }
}

/* AllocateViewport asks this for the margins to add around a viewport.
 * They are nonzero only for the full-screen space buffer of cockpit-less
 * flight on a fullscreen display that is not 4:3. */
void SdlAspectlessGetAllocationMargins(const struct Viewport *viewport,
                                       int *marginX, int *marginY)
{
    *marginX = 0;
    *marginY = 0;
    if (viewport != &stSpaceBufferViewport || bCockpitlessView <= 0 ||
        viewport->left != 0 || viewport->top != 0 ||
        viewport->right != SDL_PORT_FRAME_WIDTH - 1 ||
        viewport->bottom != SDL_PORT_FRAME_HEIGHT - 1)
        return;
    CalculateMargins(marginX, marginY);
}

/* AllocateViewport reports the buffer it allocated with margins. */
void SdlAspectlessAdoptSpaceBuffer(const struct Viewport *viewport,
                                   int marginX, int marginY)
{
    g_pWideAllocation = viewport->allocation;
    g_pWideRows = viewport->rowOffsets;
    g_nMarginX = marginX;
    g_nMarginY = marginY;
    g_bWidened = 0;
}

static int SpaceBufferIsWide(void)
{
    return g_pWideAllocation != 0 &&
           stSpaceBufferViewport.allocation == g_pWideAllocation &&
           stSpaceBufferViewport.rowOffsets == g_pWideRows;
}

static void NarrowSpaceViewport(void)
{
    if (!g_bWidened)
        return;
    g_bWidened = 0;
    if (!SpaceBufferIsWide())
        return;
    stSpaceBufferViewport.left = 0;
    stSpaceBufferViewport.top = 0;
    stSpaceBufferViewport.right = SDL_PORT_FRAME_WIDTH - 1;
    stSpaceBufferViewport.bottom = SDL_PORT_FRAME_HEIGHT - 1;
}

/* free_viewport passes the row table it is about to release and gets back
 * the start of its allocation, which lies before the table when the buffer
 * has margin rows. */
int *SdlAspectlessReleaseRows(int *rowOffsets)
{
    if (rowOffsets == 0 || rowOffsets != g_pWideRows)
        return rowOffsets;
    NarrowSpaceViewport();
    rowOffsets -= g_nMarginY;
    g_pWideAllocation = 0;
    g_pWideRows = 0;
    g_nMarginX = 0;
    g_nMarginY = 0;
    g_bWidened = 0;
    g_bBackdropValid = 0;
    return rowOffsets;
}

/* ClearViewport clears through the viewport rectangle, which leaves the
 * margins out most of the time.  Returns nonzero if it cleared the whole
 * buffer instead. */
int SdlAspectlessClearSpaceBuffer(const struct Viewport *viewport,
                                  short colour)
{
    if (viewport != &stSpaceBufferViewport || !SpaceBufferIsWide())
        return 0;
    memset(g_pWideAllocation, (unsigned char)colour,
           (size_t)(SDL_PORT_FRAME_WIDTH + 2 * g_nMarginX) *
               (SDL_PORT_FRAME_HEIGHT + 2 * g_nMarginY));
    return 1;
}

/* transform_objects_to_your_view drops whatever lies outside a cone that
 * just clears the corners of a 320x200 view at the original focal length.
 * Open it to clear the corners of the frame being drawn at the focal length
 * in use (--fov shortens it), allowing for the 50 rows by which the side
 * and rear views move the centre down. */
int SdlAspectlessViewCone(void)
{
    double focal;
    double halfWidth;
    double halfHeight;
    int cone;
    int wide;

    if (bCockpitlessView <= 0)
        return ASPECTLESS_ORIGINAL_VIEW_CONE;
    wide = SpaceBufferIsWide();
    focal = SdlFovFocal();
    if (!wide && focal >= SDL_PORT_FRAME_WIDTH / 2)
        return ASPECTLESS_ORIGINAL_VIEW_CONE;
    halfWidth = SDL_PORT_FRAME_WIDTH / 2 + (wide ? g_nMarginX : 0);
    halfHeight = SDL_PORT_FRAME_HEIGHT / 2 + (wide ? g_nMarginY : 0) + 50;
    cone = (int)(focal / sqrt(focal * focal + halfWidth * halfWidth +
                              halfHeight * halfHeight) * 256.0 * 0.9);
    if (cone > ASPECTLESS_ORIGINAL_VIEW_CONE)
        cone = ASPECTLESS_ORIGINAL_VIEW_CONE;
    return cone;
}

/* Called before a frame's objects are drawn.  Returns nonzero, with the
 * margins, if they are drawn into the widened buffer. */
int SdlAspectlessBeginSpaceFrame(int fullViewportCopy, int *marginX,
                                 int *marginY)
{
    int wantedX;
    int wantedY;

    *marginX = 0;
    *marginY = 0;
    NarrowSpaceViewport();
    if (!fullViewportCopy || stSpaceBufferViewport.pixels == 0)
        return 0;
    /* The display may have gone fullscreen, or back to a window, since the
     * buffer was allocated.  Whatever was drawn into it ahead of this
     * frame's objects is lost for the one frame. */
    SdlAspectlessGetAllocationMargins(&stSpaceBufferViewport, &wantedX,
                                      &wantedY);
    if (bViewBufferEnabled != 0 &&
        stSpaceBufferViewport.left == 0 && stSpaceBufferViewport.top == 0 &&
        stSpaceBufferViewport.right == SDL_PORT_FRAME_WIDTH - 1 &&
        stSpaceBufferViewport.bottom == SDL_PORT_FRAME_HEIGHT - 1 &&
        (wantedX != (SpaceBufferIsWide() ? g_nMarginX : 0) ||
         wantedY != (SpaceBufferIsWide() ? g_nMarginY : 0))) {
        free_viewport(&stSpaceBufferViewport);
        initialize_view_buffer();
    }
    if (!SpaceBufferIsWide() ||
        stSpaceBufferViewport.left != 0 || stSpaceBufferViewport.top != 0 ||
        stSpaceBufferViewport.right != SDL_PORT_FRAME_WIDTH - 1 ||
        stSpaceBufferViewport.bottom != SDL_PORT_FRAME_HEIGHT - 1)
        return 0;
    stSpaceBufferViewport.left = (short)-g_nMarginX;
    stSpaceBufferViewport.top = (short)-g_nMarginY;
    stSpaceBufferViewport.right =
        (short)(SDL_PORT_FRAME_WIDTH - 1 + g_nMarginX);
    stSpaceBufferViewport.bottom =
        (short)(SDL_PORT_FRAME_HEIGHT - 1 + g_nMarginY);
    g_bWidened = 1;
    *marginX = g_nMarginX;
    *marginY = g_nMarginY;
    return 1;
}

/* Called as the frame is dumped to the screen. */
void SdlAspectlessCompleteSpaceFrame(void)
{
    if (!g_bWidened || !SpaceBufferIsWide()) {
        NarrowSpaceViewport();
        g_bBackdropValid = 0;
        return;
    }
    memcpy(g_abBackdrop, g_pWideAllocation,
           (size_t)(SDL_PORT_FRAME_WIDTH + 2 * g_nMarginX) *
               (SDL_PORT_FRAME_HEIGHT + 2 * g_nMarginY));
    g_nBackdropMarginX = g_nMarginX;
    g_nBackdropMarginY = g_nMarginY;
    g_bBackdropValid = 1;
    NarrowSpaceViewport();
}

void SdlAspectlessCancelSpaceFrame(void)
{
    NarrowSpaceViewport();
    g_bBackdropValid = 0;
}

/* The frame the presenters show: 320x200 with no margins unless a widened
 * space frame is on screen. */
void SdlGetFrameGeometry(int *width, int *height, int *marginX, int *marginY)
{
    int x = g_bBackdropValid ? g_nBackdropMarginX : 0;
    int y = g_bBackdropValid ? g_nBackdropMarginY : 0;

    if (width != 0)
        *width = SDL_PORT_FRAME_WIDTH + 2 * x;
    if (height != 0)
        *height = SDL_PORT_FRAME_HEIGHT + 2 * y;
    if (marginX != 0)
        *marginX = x;
    if (marginY != 0)
        *marginY = y;
}

/* Returns the frame to present for a 320x200 screen: the screen itself, or
 * the backdrop with the screen laid over its middle. */
const unsigned char *SdlComposeFrame(const unsigned char *screen, int *width,
                                     int *height)
{
    int frameWidth;
    int frameHeight;
    int marginX;
    int marginY;
    int row;

    SdlGetFrameGeometry(&frameWidth, &frameHeight, &marginX, &marginY);
    *width = frameWidth;
    *height = frameHeight;
    if (!g_bBackdropValid || screen == 0)
        return screen;
    if (SdlEgaDitherEnabled()) {
        int x;
        int y;

        for (y = 0; y < frameHeight; y++) {
            for (x = 0; x < frameWidth; x++)
                g_abComposite[y * frameWidth + x] = SdlEgaDitherIndex(
                    g_abBackdrop[y * frameWidth + x],
                    (x + marginX + y + marginY) & 1);
        }
    } else {
        memcpy(g_abComposite, g_abBackdrop,
               (size_t)frameWidth * frameHeight);
    }
    for (row = 0; row < SDL_PORT_FRAME_HEIGHT; row++)
        memcpy(g_abComposite + (size_t)(row + marginY) * frameWidth + marginX,
               screen + (size_t)row * SDL_PORT_FRAME_WIDTH,
               SDL_PORT_FRAME_WIDTH);
    return g_abComposite;
}
