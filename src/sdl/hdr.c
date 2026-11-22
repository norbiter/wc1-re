/* Fork-only: --hdr lets the brightest light in the GL object layer go past
 * standard white on an HDR display.
 *
 * Everything the game draws stays at the brightness it has without --hdr:
 * the 320x200 frame, the cockpit, the HUD, the sprites and the 3D ships.
 * Only things that are themselves sources of light use the display's extra
 * range:
 *
 *   Suns.  With --fine-backdrop, the hot core of each sun and star in the
 *   backdrop glows above white.  Ships pass in front of it, so they hide
 *   it as they cross.
 *   Glare.  With --glare too, the haze over a large sun blooms: brightest
 *   over its disc and falling off past its edge.  A ship over the disc dims
 *   it as it already dims the haze.
 *   Gun shots.  With --fine-shots, the cores of the bolts and their trails
 *   glow above white.
 *
 * This uses macOS's extended dynamic range (EDR): the GL surface holds
 * half floats, and 1.0 stays standard white.  How far above that the
 * display can go (its headroom) depends on the display and on its
 * brightness setting, and macOS changes it while the game runs, so it is
 * read every few frames.  --hdr=PCT scales how much of the headroom is
 * used.  On other systems, and on a display without headroom, --hdr does
 * nothing. */
#include "wc1.h"

#include <math.h>

#include "video_internal.h"

#ifdef __APPLE__
#include <SDL_syswm.h>

/* The Objective-C runtime's headers define BOOL, as the game does, so the
 * two calls used are declared here.  A one-byte flag passes the same way
 * whether the runtime's BOOL is a bool or a signed char. */
typedef struct objc_selector *HdrSelector;
extern void objc_msgSend(void);
extern HdrSelector sel_registerName(const char *name);
#endif

/* The most headroom used, as a multiple of standard white in linear light.
 * Displays reach 16; a laser bolt that bright is painful. */
#define HDR_MAX_HEADROOM 8.0
/* How often the display's headroom is read again. */
#define HDR_POLL_MS 250U
/* Surface values are gamma-encoded like the rest of the frame, while the
 * headroom is linear. */
#define HDR_GAMMA 2.2f

static int g_bHdrRequested;
static int g_nHdrPercent = 100;
static int g_bHdrSurface;
static float g_fHdrPeak = 1.0f;
static Uint32 g_uHdrPolled;
#ifdef __APPLE__
typedef void *HdrObject;
static HdrObject g_pHdrWindow;
#endif

void SdlHdrEnable(int percent)
{
#ifdef __APPLE__
    g_bHdrRequested = 1;
    if (percent > 0)
        g_nHdrPercent = percent > 400 ? 400 : percent;
#else
    (void)percent;
    fprintf(stderr, "--hdr needs macOS; ignored.\n");
#endif
}

int SdlHdrRequested(void)
{
    return g_bHdrRequested;
}

/* Before the GL context is made: asks for an EDR surface on the window's
 * view.  The float pixel format itself is asked for with the other GL
 * attributes, before the window is made. */
void SdlHdrPrepareWindow(SDL_Window *window)
{
#ifdef __APPLE__
    SDL_SysWMinfo info;
    HdrObject view;
    HdrSelector setter;

    g_pHdrWindow = 0;
    g_bHdrSurface = 0;
    g_fHdrPeak = 1.0f;
    if (!g_bHdrRequested || window == 0)
        return;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) ||
        info.subsystem != SDL_SYSWM_COCOA)
        return;
    view = ((HdrObject (*)(HdrObject, HdrSelector))objc_msgSend)(
        (HdrObject)info.info.cocoa.window, sel_registerName("contentView"));
    setter = sel_registerName("setWantsExtendedDynamicRangeOpenGLSurface:");
    if (view == 0 ||
        !((unsigned char (*)(HdrObject, HdrSelector, HdrSelector))
              objc_msgSend)(
            view, sel_registerName("respondsToSelector:"), setter)) {
        fprintf(stderr, "--hdr: this macOS has no EDR for OpenGL.\n");
        return;
    }
    ((void (*)(HdrObject, HdrSelector, unsigned char))objc_msgSend)(
        view, setter, 1);
    g_pHdrWindow = (HdrObject)info.info.cocoa.window;
#else
    (void)window;
#endif
}

/* After the GL context is made: whether its surface keeps values above 1. */
void SdlHdrSetSurface(int floatSurface)
{
#ifdef __APPLE__
    g_bHdrSurface = floatSurface && g_pHdrWindow != 0;
    g_uHdrPolled = 0;
    if (g_bHdrRequested && !g_bHdrSurface)
        fprintf(stderr, "--hdr: no floating-point GL surface; HDR is off.\n");
#else
    (void)floatSurface;
#endif
}

/* Once per presented frame: reads the display's headroom now and then. */
void SdlHdrBeginFrame(void)
{
#ifdef __APPLE__
    HdrObject screen;
    double headroom;
    Uint32 now;

    if (!g_bHdrSurface)
        return;
    now = SDL_GetTicks();
    if (g_uHdrPolled != 0 && now - g_uHdrPolled < HDR_POLL_MS)
        return;
    g_uHdrPolled = now == 0 ? 1 : now;
    screen = ((HdrObject (*)(HdrObject, HdrSelector))objc_msgSend)(
        g_pHdrWindow, sel_registerName("screen"));
    headroom = 1.0;
    if (screen != 0)
        headroom = ((double (*)(HdrObject, HdrSelector))objc_msgSend)(
            screen,
            sel_registerName("maximumExtendedDynamicRangeColorComponentValue"));
    if (!(headroom > 1.0))
        headroom = 1.0;
    if (headroom > HDR_MAX_HEADROOM)
        headroom = HDR_MAX_HEADROOM;
    headroom = 1.0 + (headroom - 1.0) * g_nHdrPercent / 100.0;
    g_fHdrPeak = powf((float)headroom, 1.0f / HDR_GAMMA);
#endif
}

/* How far above standard white a light source may go this frame, as a
 * gain on top of white: 0 when there is no HDR. */
float SdlHdrGain(void)
{
    return g_bHdrSurface ? g_fHdrPeak - 1.0f : 0.0f;
}
