#include "video_internal.h"
#include "wcdata.h"

static SdlVideoBackend g_videoBackend;
static float g_afThrusterScreenX[SPACE_OBJECT_COUNT];
static float g_afThrusterScreenY[SPACE_OBJECT_COUNT];

void SdlSetThrusterScreenPosition(short object, float x, float y)
{
    g_afThrusterScreenX[object] = x;
    g_afThrusterScreenY[object] = y;
}

void SdlGetThrusterScreenPosition(short object, float *x, float *y)
{
    *x = g_afThrusterScreenX[object];
    *y = g_afThrusterScreenY[object];
}

void SdlSetVideoBackend(SdlVideoBackend backend)
{
    if (backend == SDL_PORT_VIDEO_BACKEND_GL_SHARP_BILINEAR)
        g_videoBackend = backend;
    else
        g_videoBackend = SDL_PORT_VIDEO_BACKEND_INDEXED;
}

int SdlUsingGlRenderer(void)
{
    return g_videoBackend == SDL_PORT_VIDEO_BACKEND_GL_SHARP_BILINEAR;
}

int SdlConfigureVideoWindow(Uint32 *windowFlags)
{
    if (windowFlags == 0)
        return 0;
    if (!SdlUsingGlRenderer())
        return 1;
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0 ||
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) != 0)
        return 0;
#ifdef __APPLE__
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                            SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG) != 0)
        return 0;
    /* --hdr: half floats, so light can go past standard white. */
    if (SdlHdrRequested() && SDL_GL_SetAttribute(SDL_GL_FLOATBUFFERS, 1) != 0)
        return 0;
#endif
    *windowFlags |= SDL_WINDOW_OPENGL;
    return 1;
}

void SdlCalculateOutputViewport(int width, int height, int *left,
                                   int *bottom, int *viewportWidth,
                                   int *viewportHeight)
{
    Sint64 aspectWidth;
    Sint64 aspectHeight;
    int frameWidth;
    int frameHeight;

    if (width < 1 || height < 1) {
        *left = 0;
        *bottom = 0;
        *viewportWidth = 1;
        *viewportHeight = 1;
        return;
    }

    /* Mode 13h artwork is composed for a 4:3 display, so its pixels are
     * 5:6.  An --aspectless frame has more of the same pixels. */
    SdlGetFrameGeometry(&frameWidth, &frameHeight, 0, 0);
    aspectWidth = (Sint64)frameWidth * SDL_PORT_DISPLAY_ASPECT_WIDTH *
                  SDL_PORT_FRAME_HEIGHT;
    aspectHeight = (Sint64)frameHeight * SDL_PORT_DISPLAY_ASPECT_HEIGHT *
                   SDL_PORT_FRAME_WIDTH;
    if ((Sint64)width * aspectHeight > (Sint64)height * aspectWidth) {
        *viewportHeight = height;
        *viewportWidth = (int)((Sint64)height * aspectWidth / aspectHeight);
    } else {
        *viewportWidth = width;
        *viewportHeight = (int)((Sint64)width * aspectHeight / aspectWidth);
    }
    /* An --aspectless frame was sized for this display to the nearest
     * pixel pair; fill the display rather than leave a sliver. */
    if ((frameWidth != SDL_PORT_FRAME_WIDTH ||
         frameHeight != SDL_PORT_FRAME_HEIGHT) &&
        (Sint64)*viewportWidth * 100 >= (Sint64)width * 98 &&
        (Sint64)*viewportHeight * 100 >= (Sint64)height * 98) {
        *viewportWidth = width;
        *viewportHeight = height;
    }
    if (*viewportWidth < 1)
        *viewportWidth = 1;
    if (*viewportHeight < 1)
        *viewportHeight = 1;
    *left = (width - *viewportWidth) / 2;
    *bottom = (height - *viewportHeight) / 2;
}

static int SdlGetWindowViewport(SDL_Window *window, int *left, int *top,
                                   int *viewportWidth, int *viewportHeight)
{
    int bottom;
    int height;
    int width;

    SDL_GetWindowSize(window, &width, &height);
    if (width < 1 || height < 1)
        return 0;
    SdlCalculateOutputViewport(width, height, left, &bottom,
                                  viewportWidth, viewportHeight);
    *top = height - bottom - *viewportHeight;
    return 1;
}

static int SdlScaleCoordinate(int coordinate, int sourceExtent,
                                 int destinationExtent)
{
    Sint64 numerator;

    numerator = (Sint64)coordinate * destinationExtent;
    if (numerator < 0)
        numerator -= sourceExtent / 2;
    else
        numerator += sourceExtent / 2;
    return (numerator / sourceExtent);
}

int SdlMapLogicalToWindow(SDL_Window *window, int logicalX, int logicalY,
                             int *windowX, int *windowY)
{
    int frameHeight;
    int frameWidth;
    int marginX;
    int marginY;
    int viewportHeight;
    int viewportLeft;
    int viewportTop;
    int viewportWidth;

    if (window == 0 || windowX == 0 || windowY == 0)
        return 0;
    if (!SdlGetWindowViewport(window, &viewportLeft, &viewportTop,
                                 &viewportWidth, &viewportHeight))
        return 0;
    SdlGetFrameGeometry(&frameWidth, &frameHeight, &marginX, &marginY);
    *windowX = viewportLeft +
               SdlScaleCoordinate(logicalX + marginX, frameWidth,
                                      viewportWidth);
    *windowY = viewportTop +
               SdlScaleCoordinate(logicalY + marginY, frameHeight,
                                      viewportHeight);
    return 1;
}

int SdlMapWindowToLogical(SDL_Window *window, int windowX, int windowY,
                             int *logicalX, int *logicalY)
{
    int frameHeight;
    int frameWidth;
    int marginX;
    int marginY;
    int viewportHeight;
    int viewportLeft;
    int viewportTop;
    int viewportWidth;

    if (window == 0 || logicalX == 0 || logicalY == 0)
        return 0;
    if (!SdlGetWindowViewport(window, &viewportLeft, &viewportTop,
                                 &viewportWidth, &viewportHeight))
        return 0;
    SdlGetFrameGeometry(&frameWidth, &frameHeight, &marginX, &marginY);
    *logicalX = SdlScaleCoordinate(windowX - viewportLeft, viewportWidth,
                                      frameWidth) - marginX;
    *logicalY = SdlScaleCoordinate(windowY - viewportTop, viewportHeight,
                                      frameHeight) - marginY;
    return 1;
}
