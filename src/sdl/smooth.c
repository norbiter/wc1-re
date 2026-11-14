/* Fork-only: --smooth presents space flight at three times the sim rate.
 *
 * The sim is locked to its tick (20 fps by default): rotation is whole
 * degrees per tick, the controllers decay and chase goals once per tick, and
 * timers, cooldowns and random rolls are all counted in ticks.  Rather than
 * rescale any of that, the sim keeps its tick and only the integrators are
 * stepped finer for display: after each tick, two more frames are drawn with
 * every moving object advanced by 1/3 and 2/3 of its current velocity and
 * angular rate.  These are extrapolations of the state the tick just
 * produced, so nothing is shown late; the tick itself still lands exactly
 * where it would have.
 *
 * The player's controls are polled again before each extra frame.  The
 * player's turn rate is set straight from the input (no inertia), so the
 * view answers a stick or mouse movement within one display frame.
 *
 * Everything the draw path does that belongs to the tick -- palette fades,
 * the star field, message timers, target-lock countdown, bracket flashing,
 * the rendered-frame counter -- is left out of the extra frames, and the
 * render-derived arrays the sim reads back (screen position, distance) are
 * put back afterwards as the tick's own frame left them.
 *
 * Only cockpit and cockpit-less views are handled.  Chase and external views
 * integrate their own camera, so they, scripted views, frame skipping and the
 * training simulator fall back to one frame per tick. */
#include "wc1.h"

#include <math.h>

#define SMOOTH_SUBFRAMES 3
#define SMOOTH_DEGREES_TO_RADIANS 0.017453292519943295

static int g_bSmoothEnabled;
static int g_bSmoothSubFrame;

typedef struct SmoothSavedState {
    FixedVector positions[SPACE_OBJECT_COUNT];
    FixedVector forward[SPACE_OBJECT_COUNT];
    FixedVector up[SPACE_OBJECT_COUNT];
    FixedVector right[SPACE_OBJECT_COUNT];
    FixedVector velocity[SPACE_OBJECT_COUNT];
    FixedVector viewPositions[SPACE_OBJECT_COUNT];
    short screenX[SPACE_OBJECT_COUNT];
    short screenY[SPACE_OBJECT_COUNT];
    unsigned short distances[SPACE_OBJECT_COUNT];
    short previousDistances[SPACE_OBJECT_COUNT];
    short scales[SPACE_OBJECT_COUNT];
    short angles[SPACE_OBJECT_COUNT];
    short viewFrames[SPACE_OBJECT_COUNT];
    short flips[SPACE_OBJECT_COUNT];
    short closestVisible;
} SmoothSavedState;

static SmoothSavedState g_stSmoothSaved;

void SdlSmoothEnable(void)
{
    g_bSmoothEnabled = 1;
}

int SdlSmoothSubFrame(void)
{
    return g_bSmoothSubFrame;
}

static int SmoothEligible(void)
{
    return g_bSmoothEnabled &&
           !SdlSwcFlightActive() &&
           nFrameSkip == 1 &&
           nCameraViewMode >= 0 && nCameraViewMode <= 3 &&
           bScriptedView != 1 &&
           nTrainSimActive == 0 &&
           nCannedSceneMode == 0 &&
           nArcadeState == 0 &&
           aeObjectClass[0] != OBJECT_CLASS_NULL;
}

/* The pacing throttle spaces presents by nFrameIntervalMs.  A smooth tick
 * splits the tick interval over its three presents; any other tick keeps the
 * whole interval for its single present. */
void SdlSmoothBeginPresent(int index)
{
    long full;

    if (!g_bSmoothEnabled)
        return;
    full = SdlGetSpaceFlightTickIntervalMs();
    if (!SmoothEligible())
        nFrameIntervalMs = full;
    else if (index < SMOOTH_SUBFRAMES - 1)
        nFrameIntervalMs = full / SMOOTH_SUBFRAMES;
    else
        nFrameIntervalMs = full - (SMOOTH_SUBFRAMES - 1) *
                                      (full / SMOOTH_SUBFRAMES);
}

static void RotateAboutI(FixedVector *j, FixedVector *k, double degrees)
{
    double cosine = cos(degrees * SMOOTH_DEGREES_TO_RADIANS);
    double sine = sin(degrees * SMOOTH_DEGREES_TO_RADIANS);
    double old;

    old = j->x;
    j->x = (int)(old * cosine - k->x * sine);
    k->x = (int)(old * sine + k->x * cosine);
    old = j->y;
    j->y = (int)(old * cosine - k->y * sine);
    k->y = (int)(old * sine + k->y * cosine);
    old = j->z;
    j->z = (int)(old * cosine - k->z * sine);
    k->z = (int)(old * sine + k->z * cosine);
}

static void RotateAboutJ(FixedVector *i, FixedVector *k, double degrees)
{
    double cosine = cos(degrees * SMOOTH_DEGREES_TO_RADIANS);
    double sine = sin(degrees * SMOOTH_DEGREES_TO_RADIANS);
    double old;

    old = i->x;
    i->x = (int)(k->x * sine + old * cosine);
    k->x = (int)(k->x * cosine - old * sine);
    old = i->y;
    i->y = (int)(k->y * sine + old * cosine);
    k->y = (int)(k->y * cosine - old * sine);
    old = i->z;
    i->z = (int)(k->z * sine + old * cosine);
    k->z = (int)(k->z * cosine - old * sine);
}

static void RotateAboutK(FixedVector *i, FixedVector *j, double degrees)
{
    double cosine = cos(degrees * SMOOTH_DEGREES_TO_RADIANS);
    double sine = sin(degrees * SMOOTH_DEGREES_TO_RADIANS);
    double old;

    old = i->x;
    i->x = (int)(old * cosine - j->x * sine);
    j->x = (int)(old * sine + j->x * cosine);
    old = i->y;
    i->y = (int)(old * cosine - j->y * sine);
    j->y = (int)(old * sine + j->y * cosine);
    old = i->z;
    i->z = (int)(old * cosine - j->z * sine);
    j->z = (int)(old * sine + j->z * cosine);
}

/* The same step order as rotate_object, with the angles scaled by the
 * fraction of a tick: pitch, yaw, then roll, each followed by the
 * re-orthogonalisation. */
static void AdvanceOrientation(short obj, double fraction)
{
    if (anObjPitchRot[obj] != 0) {
        RotateAboutI(&aShipUpVector[obj], &aShipFwdVec[obj],
                     anObjPitchRot[obj] * fraction);
        fix_objects_ijk(obj);
    }
    if (anObjectYawRotation[obj] != 0) {
        RotateAboutJ(&aShipRightVector[obj], &aShipFwdVec[obj],
                     anObjectYawRotation[obj] * fraction);
        fix_objects_ijk(obj);
    }
    if (anObjectRollRotationList[obj] != 0) {
        RotateAboutK(&aShipRightVector[obj], &aShipUpVector[obj],
                     anObjectRollRotationList[obj] * fraction);
        fix_objects_ijk(obj);
    }
}

static void SaveState(void)
{
    SmoothSavedState *saved = &g_stSmoothSaved;

    memcpy(saved->positions, aShipPositionsArray, sizeof(saved->positions));
    memcpy(saved->forward, aShipFwdVec, sizeof(saved->forward));
    memcpy(saved->up, aShipUpVector, sizeof(saved->up));
    memcpy(saved->right, aShipRightVector, sizeof(saved->right));
    memcpy(saved->velocity, aShipVelocity, sizeof(saved->velocity));
    memcpy(saved->viewPositions, aObjectViewPositionsList,
           sizeof(saved->viewPositions));
    memcpy(saved->screenX, asObjectScreenXList, sizeof(saved->screenX));
    memcpy(saved->screenY, asObjectScreenYArray, sizeof(saved->screenY));
    memcpy(saved->distances, asObjDistancesTable, sizeof(saved->distances));
    memcpy(saved->previousDistances, asPreviousObjectDistance,
           sizeof(saved->previousDistances));
    memcpy(saved->scales, asObjScreenScalesArray, sizeof(saved->scales));
    memcpy(saved->angles, asObjScreenAngles, sizeof(saved->angles));
    memcpy(saved->viewFrames, asObjectViewFrames, sizeof(saved->viewFrames));
    memcpy(saved->flips, asObjectFlipArray, sizeof(saved->flips));
    saved->closestVisible = nClosestVisibleObject;
}

static void RestoreState(void)
{
    SmoothSavedState *saved = &g_stSmoothSaved;

    memcpy(aShipPositionsArray, saved->positions, sizeof(saved->positions));
    memcpy(aShipFwdVec, saved->forward, sizeof(saved->forward));
    memcpy(aShipUpVector, saved->up, sizeof(saved->up));
    memcpy(aShipRightVector, saved->right, sizeof(saved->right));
    memcpy(aShipVelocity, saved->velocity, sizeof(saved->velocity));
    memcpy(aObjectViewPositionsList, saved->viewPositions,
           sizeof(saved->viewPositions));
    memcpy(asObjectScreenXList, saved->screenX, sizeof(saved->screenX));
    memcpy(asObjectScreenYArray, saved->screenY, sizeof(saved->screenY));
    memcpy(asObjDistancesTable, saved->distances, sizeof(saved->distances));
    memcpy(asPreviousObjectDistance, saved->previousDistances,
           sizeof(saved->previousDistances));
    memcpy(asObjScreenScalesArray, saved->scales, sizeof(saved->scales));
    memcpy(asObjScreenAngles, saved->angles, sizeof(saved->angles));
    memcpy(asObjectViewFrames, saved->viewFrames, sizeof(saved->viewFrames));
    memcpy(asObjectFlipArray, saved->flips, sizeof(saved->flips));
    nClosestVisibleObject = saved->closestVisible;
}

/* Move every object, and the eye that follows the player, to its pose a
 * fraction of a tick past the one the sim left. */
static void AdvanceWorld(double fraction)
{
    short obj;

    for (obj = 0; obj <= SPACE_LAST_MOVING_OBJECT; obj++) {
        if (aeObjectClass[obj] <= OBJECT_CLASS_PLANET)
            continue;
        aShipPositionsArray[obj].x +=
            (int)(aShipVelocity[obj].x * fraction);
        aShipPositionsArray[obj].y +=
            (int)(aShipVelocity[obj].y * fraction);
        aShipPositionsArray[obj].z +=
            (int)(aShipVelocity[obj].z * fraction);
        if (aeObjectClass[obj] >= OBJECT_CLASS_PROJECTILE)
            AdvanceOrientation(obj, fraction);
    }
    /* The cockpit views only copy the player's frame (see
       set_eye_direction_and_position), so the eye is rebuilt here without
       calling it. */
    switch (nCameraViewMode) {
    case 0:
        copy_frame(0, EYE_OBJECT);
        break;
    case 1:
        aShipRightVector[EYE_OBJECT] = aShipFwdVec[0];
        negate_vector(&aShipRightVector[EYE_OBJECT]);
        aShipUpVector[EYE_OBJECT] = aShipUpVector[0];
        aShipFwdVec[EYE_OBJECT] = aShipRightVector[0];
        break;
    case 2:
        aShipRightVector[EYE_OBJECT] = aShipFwdVec[0];
        aShipUpVector[EYE_OBJECT] = aShipUpVector[0];
        aShipFwdVec[EYE_OBJECT] = aShipRightVector[0];
        negate_vector(&aShipFwdVec[EYE_OBJECT]);
        break;
    case 3:
        copy_frame(0, EYE_OBJECT);
        negate_vector(&aShipRightVector[EYE_OBJECT]);
        negate_vector(&aShipFwdVec[EYE_OBJECT]);
        break;
    }
    aShipPositionsArray[EYE_OBJECT] = aShipPositionsArray[0];
}

/* house_keep_objects removes the exhaust and turret children at the start
 * of every tick and the draw path rebuilds them, so a repeat draw does the
 * same; place_exhaust_on_ships then finds them in the new place. */
static void RemoveFixedChildren(void)
{
    short obj;

    for (obj = 0; obj <= SPACE_LAST_MOVING_OBJECT; obj++) {
        if (aeObjectClass[obj] == OBJECT_CLASS_FIXED_OBJECT &&
            (aeObjectTypeList[obj] == OBJECT_TYPE_TURRET ||
             aeObjectTypeList[obj] == OBJECT_TYPE_THRUSTERS))
            remove_object(obj);
    }
}

/* Without a cockpit the dump replaces the whole screen, so what
 * update_cockpit draws over it (VDUs, scanner, bars) is lost on every extra
 * frame.  Remember which pixels it changed and lay them over again. */
#define SMOOTH_SCREEN_BYTES 64000
static unsigned char g_abHudBefore[SMOOTH_SCREEN_BYTES];
static unsigned char g_abHudAfter[SMOOTH_SCREEN_BYTES];
static unsigned char g_abHudMask[SMOOTH_SCREEN_BYTES];
static int g_bHudValid;

void SdlSmoothHudBegin(void)
{
    g_bHudValid = 0;
    if (!g_bSmoothEnabled || bCockpitlessView <= 0 ||
        stScreenView.pixels == 0)
        return;
    memcpy(g_abHudBefore, stScreenView.pixels, SMOOTH_SCREEN_BYTES);
}

void SdlSmoothHudEnd(void)
{
    int i;

    if (!g_bSmoothEnabled || bCockpitlessView <= 0 ||
        stScreenView.pixels == 0)
        return;
    memcpy(g_abHudAfter, stScreenView.pixels, SMOOTH_SCREEN_BYTES);
    for (i = 0; i < SMOOTH_SCREEN_BYTES; i++)
        g_abHudMask[i] = g_abHudAfter[i] != g_abHudBefore[i];
    g_bHudValid = 1;
}

static void ReapplyHud(void)
{
    int i;

    if (!g_bHudValid || bCockpitlessView <= 0 || stScreenView.pixels == 0)
        return;
    for (i = 0; i < SMOOTH_SCREEN_BYTES; i++) {
        if (g_abHudMask[i])
            stScreenView.pixels[i] = g_abHudAfter[i];
    }
}

/* Draw_3Space_Frame and RenderSpaceViewFrame without what belongs to the
 * tick: the palette fade, the frame-skip and rendered-frame counters, the
 * star field, check_message and the arcade score panel. */
static void DrawExtraFrame(void)
{
    transform_objects_to_your_view();
    place_exhaust_on_ships();
    reposition_fixed_child_objects();
    sort_object_depth();
    SdlBeginSpaceFrame(
        pScreenViewGeometry,
        (int)cScreenViewMode,
        bCockpitlessView > 0,
        (unsigned char)cPrimaryViewBufferColour);
    SdlDustDraw(&stSpaceBufferViewport);
    SdlGlareBeginFrame();
    draw_sorted_objects_to_buffer();
    SdlGlareDraw(&stSpaceBufferViewport);
    if (nCameraViewMode == 0)
        overlay_head_up_display();
    RestoreCockpitExplosionIfVisible();
    dump_buffer_to_screen();
    ReapplyHud();
    if (nCameraViewMode == 0)
        RestoreTransientCockpitGraphics();
    ClearViewport(&stSpaceBufferViewport, cPrimaryViewBufferColour);
}

/* Called right after the tick's own frame has been presented.  Returns
 * nonzero if the player's controls asked to leave flight. */
int SdlSmoothFlightSubFrames(void)
{
    int index;

    for (index = 1; index < SMOOTH_SUBFRAMES; index++) {
        if (!SmoothEligible())
            return 0;
        if (HandleSpaceFlightControls() == -1)
            return 1;
        if (!SmoothEligible())
            return 0;
        SaveState();
        AdvanceWorld((double)index / SMOOTH_SUBFRAMES);
        RemoveFixedChildren();
        g_bSmoothSubFrame = 1;
        DrawExtraFrame();
        g_bSmoothSubFrame = 0;
        RestoreState();
        SdlSmoothBeginPresent(index);
        DIBslam();
        DIBslamReal();
    }
    return 0;
}
