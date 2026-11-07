/* Fork-only: --mouse-curve[=PCT] and --joystick-curve[=PCT] replace the original's stepped mapping from
 * pointer offset to yaw/pitch with a smooth curve that is mostly linear near
 * the centre and bends exponentially towards full deflection, so small
 * adjustments stay small and a large flick reaches the maximum.
 *
 * The original looks the offset up in two uneven threshold tables (yaw 10/37/
 * 52/57/62 px, pitch 5/18/27/35/38 px), packing most of the range into the
 * first few pixels.  The game still receives its -8..8 steps, so turn rates
 * stay those of the craft.  S scales the pointer travel needed for full
 * deflection: above 1 is more sensitive, below 1 less (default 1).
 *
 * --joystick-curve applies the same shape to gamepad and joystick axes: the
 * main stick is reshaped before the game's own calibration sees it (the
 * calibration still quantises to its 9 steps and keeps its one-step dead
 * zone), and the extra axes get a smaller dead zone than the fixed 25%. */
#include "wc1.h"

#define MOUSE_CURVE_MAX_STEP 8
/* Offsets (logical 320x200 pixels) that give full deflection at S = 1. */
#define MOUSE_CURVE_YAW_RANGE 80.0f
/* Logical pixels are 1.2 times taller than wide on the 4:3 display, so this
 * gives pitch and yaw the same physical mouse travel. */
#define MOUSE_CURVE_PITCH_RANGE (MOUSE_CURVE_YAW_RANGE / 1.2f)
/* Share of the curve that is exponential, and how sharply it bends. */
#define MOUSE_CURVE_EXP_SHARE 0.7f
#define MOUSE_CURVE_EXP_BEND 3.0f

/* Joystick dead zone as a fraction of full travel. */
#define JOYSTICK_CURVE_DEADZONE 0.08f

static int g_bMouseCurve;
static int g_bJoystickCurve;
static float g_fJoystickCurveScale = 1.0f;
static float g_fMouseCurveScale = 1.0f;

void SdlMouseCurveEnable(int percent)
{
    g_bMouseCurve = 1;
    if (percent > 0)
        g_fMouseCurveScale = (float)percent / 100.0f;
}

int SdlMouseCurveActive(void)
{
    return g_bMouseCurve;
}

/* Maps a 0..1 fraction of full deflection to a 0..1 fraction. */
static float SdlInputCurveShape(float u)
{
    if (u > 1.0f)
        u = 1.0f;
    return (1.0f - MOUSE_CURVE_EXP_SHARE) * u +
           MOUSE_CURVE_EXP_SHARE *
               (expf(MOUSE_CURVE_EXP_BEND * u) - 1.0f) /
               (expf(MOUSE_CURVE_EXP_BEND) - 1.0f);
}

/* offset: pointer distance from the viewport centre in logical pixels;
 * vertical selects the pitch range.  Returns -8..8. */
short SdlMouseCurveStep(short offset, int vertical)
{
    float u;
    short step;

    u = (float)(offset < 0 ? -offset : offset) * g_fMouseCurveScale /
        (vertical ? MOUSE_CURVE_PITCH_RANGE : MOUSE_CURVE_YAW_RANGE);
    step = (short)(SdlInputCurveShape(u) * MOUSE_CURVE_MAX_STEP + 0.5f);
    return offset < 0 ? (short)-step : step;
}

void SdlJoystickCurveEnable(int percent)
{
    g_bJoystickCurve = 1;
    if (percent > 0)
        g_fJoystickCurveScale = (float)percent / 100.0f;
}

int SdlJoystickCurveActive(void)
{
    return g_bJoystickCurve;
}

/* Reshapes a raw SDL axis value; the result has the same -32767..32767 range
 * and the same centre and end points. */
Sint16 SdlJoystickCurveAxis(Sint16 raw)
{
    float magnitude;
    float u;
    int shaped;

    magnitude = (float)(raw < 0 ? -(int)raw : (int)raw) / 32767.0f;
    if (magnitude <= JOYSTICK_CURVE_DEADZONE)
        return 0;
    u = (magnitude - JOYSTICK_CURVE_DEADZONE) /
        (1.0f - JOYSTICK_CURVE_DEADZONE) * g_fJoystickCurveScale;
    shaped = (int)(SdlInputCurveShape(u) * 32767.0f + 0.5f);
    return (Sint16)(raw < 0 ? -shaped : shaped);
}
