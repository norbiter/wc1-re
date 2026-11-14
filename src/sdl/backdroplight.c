/* Fork-only: --backdrop-light lights the 3D ships' rim from the direction
 * the backdrop implies.
 *
 * Each campaign series places up to four backdrop sprites around the sky.
 * The planets were painted lit from one side (series 1's blue planet from
 * above, most others from the upper left), and the suns are light sources
 * themselves.  Without this switch the ships' rim light comes from a fixed
 * place behind them on the screen; with it, the rim light is fixed in space,
 * so as the ship turns, the edges that catch it are the ones facing the
 * backdrop's light.
 *
 * Each shape id has a light: a sun lights from its own direction, a planet
 * from the side its sprite shows lit (worked out once from the sprites as a
 * direction in the sprite's own frame, toward the viewer for a mostly lit
 * disc).  The sprite's frame in space is the one the game turns it by when
 * it draws it.  When a series has several, a large sun wins, then the
 * largest planet, then the largest small sun; nebulae, the galaxy, asteroids
 * and comets cast nothing.  A series with none keeps the fixed rim light.
 *
 * Where a planet's painting disagrees with its series' light, the planet can
 * be listed to have its sprite turned so its lit side faces that light; the
 * turn applies to the plain sprite and to --fine-backdrop alike.  Only the
 * side turns: a planet painted mostly lit from the viewer's side stays so,
 * wherever its sun is. */
#include "wc1.h"

#include <math.h>

#define BACKDROP_LIGHT_SHAPES 28

enum {
    LIGHT_NONE,
    LIGHT_SMALL_SUN,
    LIGHT_PLANET,
    LIGHT_LARGE_SUN
};

typedef struct BackdropLightShape {
    unsigned char kind;
    /* The sprite's larger side, to rank lights of one kind. */
    unsigned char size;
    /* For a planet: towards its light, in the sprite's frame (x right,
     * y down, z towards the viewer). */
    float x;
    float y;
    float z;
} BackdropLightShape;

static const BackdropLightShape g_aBackdropLightShapes[BACKDROP_LIGHT_SHAPES] = {
    { LIGHT_NONE, 38, 0, 0, 0 },                       /* 0: violet nebula */
    { LIGHT_NONE, 46, 0, 0, 0 },                       /* 1: red nebula */
    { LIGHT_NONE, 49, 0, 0, 0 },                       /* 2: spiral galaxy */
    { LIGHT_PLANET, 51, -0.36f, -0.85f, 0.39f },       /* 3: blue planet */
    { LIGHT_SMALL_SUN, 24, 0, 0, 0 },                  /* 4: white star */
    { LIGHT_PLANET, 49, -0.96f, -0.26f, 0.0f },        /* 5: banded giant */
    { LIGHT_LARGE_SUN, 44, 0, 0, 0 },                  /* 6: red sun */
    { LIGHT_PLANET, 41, -0.88f, -0.37f, 0.28f },       /* 7: red planet */
    { LIGHT_PLANET, 49, -0.72f, 0.55f, 0.43f },        /* 8: ringed planet */
    { LIGHT_SMALL_SUN, 27, 0, 0, 0 },                  /* 9: yellow sun */
    { LIGHT_LARGE_SUN, 41, 0, 0, 0 },                  /* 10: red giant */
    { LIGHT_NONE, 0, 0, 0, 0 },                        /* 11: unused */
    { LIGHT_PLANET, 68, -0.80f, -0.40f, 0.46f },       /* 12: planet, moons */
    { LIGHT_PLANET, 20, -0.60f, -0.60f, 0.53f },       /* 13: planet, moon */
    { LIGHT_LARGE_SUN, 48, 0, 0, 0 },                  /* 14: large yellow */
    { LIGHT_SMALL_SUN, 22, 0, 0, 0 },                  /* 15: small yellow */
    { LIGHT_PLANET, 44, -0.11f, -0.25f, 0.96f },       /* 16: orange planet */
    { LIGHT_PLANET, 19, -0.54f, -0.65f, 0.54f },       /* 17: small orange */
    { LIGHT_SMALL_SUN, 22, 0, 0, 0 },                  /* 18: speckled red */
    { LIGHT_PLANET, 41, -0.88f, -0.37f, 0.28f },       /* 19: green planet */
    { LIGHT_PLANET, 16, -0.83f, -0.56f, 0.0f },        /* 20: small green */
    { LIGHT_SMALL_SUN, 37, 0, 0, 0 },                  /* 21: blue-white */
    { LIGHT_SMALL_SUN, 16, 0, 0, 0 },                  /* 22: small b-w */
    { LIGHT_NONE, 67, 0, 0, 0 },                       /* 23: asteroid */
    { LIGHT_NONE, 40, 0, 0, 0 },                       /* 24: asteroids */
    { LIGHT_NONE, 17, 0, 0, 0 },                       /* 25: asteroids */
    { LIGHT_NONE, 33, 0, 0, 0 },                       /* 26: comet */
    { LIGHT_NONE, 68, 0, 0, 0 },                       /* 27: comet */
};

typedef struct BackdropLightObject {
    struct Shape *shape;
    short shapeId;
    /* Unit vectors in the game's world frame: towards the object, and the
     * sprite's right and down as the game draws it. */
    float toward[3];
    float right[3];
    float down[3];
    /* Towards the light, unit length. */
    float light[3];
    /* Turn this planet's sprite so its lit side faces the series' light;
     * turnReady once turnDegrees is known. */
    int turnToSun;
    int turnReady;
    short turnDegrees;
} BackdropLightObject;

/* Planets turned to face their series' light, where the painting and the
 * sky disagree.  Scene is the series' index in the campaign's
 * constellation table, which each campaign has its own of; the pairs are
 * told apart by shape, as ids 12 and up appear only in Secret Missions 2.
 * Secret Missions 1 repeats the original's series 7, so it is turned
 * there too. */
typedef struct BackdropLightTurn {
    short scene;
    short shapeId;
} BackdropLightTurn;

static const BackdropLightTurn g_aBackdropLightTurns[] = {
    /* Original campaign. */
    { 6, 7 },  /* Series 7: the red planet, to the ringed planet's light. */
    { 10, 3 }, /* Series 11: the blue planet, to the red giant. */
    { 12, 7 }, /* Series 13: the red planet, to the banded giant's light. */
    /* Secret Missions 2, all to the large yellow sun. */
    { 0, 17 }, /* Series 1: the small orange planet. */
    { 1, 16 }, /* Series 2: the orange planet. */
    { 1, 13 }, /* Series 2: the planet with a moon. */
    { 2, 17 }, /* Series 3: the small orange planet. */
};

static int g_bBackdropLightEnabled;
static BackdropLightObject g_aBackdropLightObjects[SPACE_OBJECT_COUNT];

void SdlBackdropLightEnable(void)
{
    g_bBackdropLightEnabled = 1;
}

static void FixedToUnit(const FixedVector *vector, float out[3])
{
    float length;

    out[0] = (float)vector->x;
    out[1] = (float)vector->y;
    out[2] = (float)vector->z;
    length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (length <= 0.0f) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    out[0] /= length;
    out[1] /= length;
    out[2] /= length;
}

static float DotLight(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* A planet's light from its painted direction (x, y) in the sprite's frame
 * and its share z towards the viewer. */
static int SetPlanetLight(BackdropLightObject *entry, float x, float y,
                          float z)
{
    float length;
    int axis;

    for (axis = 0; axis < 3; axis++)
        entry->light[axis] = x * entry->right[axis] + y * entry->down[axis] -
                             z * entry->toward[axis];
    length = sqrtf(DotLight(entry->light, entry->light));
    if (length <= 0.0f)
        return 0;
    for (axis = 0; axis < 3; axis++)
        entry->light[axis] /= length;
    return 1;
}

/* Called once the game has placed constellation object obj from entry
 * definitionIndex of the campaign's constellation table.  Works out where
 * its light comes from, in the frame the game draws its sprite in:
 * set_background_objects_rotation turns the sprite so that its down is the
 * up vector of a frame yawed and pitched to face the viewer. */
void SdlBackdropLightNoteObject(short obj, short shapeId,
                                short definitionIndex)
{
    BackdropLightObject *entry;
    const BackdropLightShape *look;
    FixedVector toViewer;
    SphericalVector spherical;
    unsigned int turn;
    int axis;

    if (obj < 0 || obj >= SPACE_OBJECT_COUNT)
        return;
    entry = &g_aBackdropLightObjects[obj];
    memset(entry, 0, sizeof(*entry));
    if (!g_bBackdropLightEnabled || shapeId < 0 ||
        shapeId >= BACKDROP_LIGHT_SHAPES)
        return;
    look = &g_aBackdropLightShapes[shapeId];
    if (look->kind == LIGHT_NONE)
        return;
    entry->shape = apObjShapeArray[obj];
    entry->shapeId = shapeId;
    FixedToUnit(&aShipPositionsArray[obj], entry->toward);
    if (look->kind != LIGHT_PLANET) {
        memcpy(entry->light, entry->toward, sizeof(entry->light));
        return;
    }

    /* Object 63 is the game's own scratch frame for this. */
    toViewer = aShipPositionsArray[obj];
    negate_vector(&toViewer);
    rectangular_to_spherical(&toViewer, &spherical);
    init_ijk(63);
    alter_yaw(-spherical.yaw, 63);
    alter_pitch(-spherical.pitch, 63);
    FixedToUnit(&aShipRightVector[63], entry->right);
    FixedToUnit(&aShipUpVector[63], entry->down);
    for (axis = 0; axis < 3; axis++)
        entry->right[axis] = -entry->right[axis];
    if (!SetPlanetLight(entry, look->x, look->y, look->z)) {
        entry->shape = 0;
        return;
    }
    for (turn = 0; turn < sizeof(g_aBackdropLightTurns) /
                              sizeof(g_aBackdropLightTurns[0]);
         turn++) {
        if (definitionIndex / 4 == g_aBackdropLightTurns[turn].scene &&
            shapeId == g_aBackdropLightTurns[turn].shapeId)
            entry->turnToSun = 1;
    }
}

/* The series' light: a large sun first, then the largest planet, then the
 * largest small sun. */
static BackdropLightObject *FindBackdropLight(void)
{
    BackdropLightObject *best = 0;
    int bestScore = 0;
    int obj;

    for (obj = 0; obj < SPACE_OBJECT_COUNT; obj++) {
        BackdropLightObject *entry = &g_aBackdropLightObjects[obj];
        const BackdropLightShape *look;
        int score;

        if (entry->shape == 0 || aeObjectClass[obj] != OBJECT_CLASS_PLANET ||
            apObjShapeArray[obj] != entry->shape)
            continue;
        look = &g_aBackdropLightShapes[entry->shapeId];
        score = look->kind * 256 + look->size;
        if (score > bestScore) {
            bestScore = score;
            best = entry;
        }
    }
    return best;
}

/* The angle to draw backdrop object obj's sprite at, given the game's own:
 * turned further for a planet listed to face its series' light. */
short SdlBackdropLightTurnSprite(short obj, short angle)
{
    BackdropLightObject *entry;
    const BackdropLightShape *look;
    const BackdropLightObject *sun;
    float sunX;
    float sunY;
    float turn;
    float cosine;
    float sine;
    int turned;

    if (!g_bBackdropLightEnabled || obj < 0 || obj >= SPACE_OBJECT_COUNT)
        return angle;
    entry = &g_aBackdropLightObjects[obj];
    if (!entry->turnToSun || entry->shape == 0 ||
        apObjShapeArray[obj] != entry->shape)
        return angle;
    if (!entry->turnReady) {
        sun = FindBackdropLight();
        if (sun == 0 || sun == entry)
            return angle;
        look = &g_aBackdropLightShapes[entry->shapeId];
        sunX = DotLight(sun->light, entry->right);
        sunY = DotLight(sun->light, entry->down);
        if (sunX * sunX + sunY * sunY < 1.0e-4f)
            return angle;
        turn = atan2f(sunY, sunX) - atan2f(look->y, look->x);
        cosine = cosf(turn);
        sine = sinf(turn);
        SetPlanetLight(entry, look->x * cosine - look->y * sine,
                       look->x * sine + look->y * cosine, look->z);
        entry->turnDegrees = (short)floorf(turn * 57.29578f + 0.5f);
        entry->turnReady = 1;
    }
    turned = (angle + entry->turnDegrees) % 360;
    if (turned < 0)
        turned += 360;
    return (short)turned;
}

/* The backdrop's light in the eye's frame (x right, y down, z forward),
 * towards the light; 0 when the backdrop implies none. */
int SdlBackdropLightCamera(float light[3])
{
    const BackdropLightObject *best;
    float eye[3][3];
    int axis;

    if (!g_bBackdropLightEnabled)
        return 0;
    best = FindBackdropLight();
    if (best == 0)
        return 0;
    FixedToUnit(&aShipRightVector[EYE_OBJECT], eye[0]);
    FixedToUnit(&aShipUpVector[EYE_OBJECT], eye[1]);
    FixedToUnit(&aShipFwdVec[EYE_OBJECT], eye[2]);
    for (axis = 0; axis < 3; axis++)
        light[axis] = DotLight(best->light, eye[axis]);
    return 1;
}
