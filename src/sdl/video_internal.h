#ifndef SDL_PORT_VIDEO_INTERNAL_H
#define SDL_PORT_VIDEO_INTERNAL_H

#include "wc1sdl.h"

#define SDL_PORT_FRAME_WIDTH 320
#define SDL_PORT_FRAME_HEIGHT 200
/* --aspectless adds margins around the frame, on two opposite sides only. */
#define SDL_PORT_FRAME_MAX_WIDTH 864
#define SDL_PORT_FRAME_MAX_HEIGHT 480
#define SDL_PORT_FRAME_MAX_PIXELS \
    (SDL_PORT_FRAME_MAX_WIDTH * SDL_PORT_FRAME_HEIGHT)
#define SDL_PORT_DISPLAY_ASPECT_WIDTH 4
#define SDL_PORT_DISPLAY_ASPECT_HEIGHT 3

typedef enum SdlVideoBackend {
    SDL_PORT_VIDEO_BACKEND_INDEXED,
    SDL_PORT_VIDEO_BACKEND_GL_SHARP_BILINEAR
} SdlVideoBackend;

void SdlSetVideoBackend(SdlVideoBackend backend);
int SdlUsingGlRenderer(void);
void SdlEnableEgaDither(void);
int SdlEgaDitherEnabled(void);
unsigned char SdlEgaDitherIndex(unsigned char colour, int odd);
void SdlGetFrameGeometry(int *width, int *height, int *marginX, int *marginY);
const unsigned char *SdlComposeFrame(const unsigned char *screen, int *width,
                                     int *height);
int SdlAspectlessBeginSpaceFrame(int fullViewportCopy, int *marginX,
                                 int *marginY);
void SdlAspectlessCompleteSpaceFrame(void);
void SdlAspectlessCancelSpaceFrame(void);
int SdlShip3dUsesGl(void);
int SdlGlRendererRecordShip(const struct Viewport *viewport,
                               const float *points, int count,
                               float footprintHalfExtent, int trueColour);
int SdlGlRendererRecordShipOverlay(const struct Viewport *viewport,
                                   const float *vertices, int count,
                                   const float colour[3]);
int SdlGlRendererRecordColourOverlay(const struct Viewport *viewport,
                                     const float *vertices, int count);
int SdlGlRendererCreateImage(int width, int height,
                             const unsigned char *pixels);
void SdlGlRendererReleaseImage(int handle);
unsigned int SdlGlRendererImageGeneration(void);
int SdlGlRendererRecordImage(const struct Viewport *viewport, int handle,
                             const float corners[8], const float tint[3]);
void SdlGlRendererSetLastHdrGain(float gain);
int SdlHdrRequested(void);
void SdlHdrPrepareWindow(SDL_Window *window);
void SdlHdrSetSurface(int floatSurface);
void SdlHdrBeginFrame(void);
float SdlHdrGain(void);
int SdlConfigureVideoWindow(Uint32 *windowFlags);
void SdlCalculateOutputViewport(int width, int height, int *left,
                                   int *bottom, int *viewportWidth,
                                   int *viewportHeight);
int SdlMapLogicalToWindow(SDL_Window *window, int logicalX, int logicalY,
                             int *windowX, int *windowY);
int SdlMapWindowToLogical(SDL_Window *window, int windowX, int windowY,
                             int *logicalX, int *logicalY);

int SdlGlRendererInitialize(SDL_Window *window);
void SdlGlRendererShutdown(void);
int SdlGlRendererPresent(const unsigned char *pixels,
                            const unsigned char *palette);
void SdlGlRendererWaitForVerticalBlank(void);
float SdlGlRendererOutputScale(void);
void SdlGlRendererBeginSpaceFrame(
    const struct ScreenViewportGeometry *geometry, int viewportMode,
    int fullViewportCopy, unsigned char clearColour, int marginX,
    int marginY);
void SdlGlRendererCompleteSpaceFrame(void);
void SdlGlRendererCancelSpaceFrame(void);
int SdlGlRendererRecordSpaceSprite(const struct Viewport *viewport, float x,
                                      float y, struct Shape *shape,
                                      short frame, short angle, short scale,
                                      short flip);

/* --ttf-text: one TrueType glyph, in 320x200 frame coordinates, with its
 * place in the glyph atlas and its palette entry. */
typedef struct SdlTtfTextQuad {
    float left;
    float top;
    float right;
    float bottom;
    float u0;
    float v0;
    float u1;
    float v1;
    unsigned char colour;
} SdlTtfTextQuad;

int SdlTtfTextEnabled(void);
int SdlTtfTextPrepare(const unsigned char *screen,
                      const unsigned char *mouseFree, unsigned char *erased,
                      float scaleX, float scaleY);
const SdlTtfTextQuad *SdlTtfTextQuads(int *count);
const unsigned char *SdlTtfTextAtlas(int *size, unsigned int *generation);

#endif
