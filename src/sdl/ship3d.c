/* Fork-only: draw ships from point-cloud models instead of blitting the
 * 37 pre-rendered sprite views.
 *
 * The models are binary PLY files named SHIP_Vnn.ply or SHIPTYPE_Vnn.ply,
 * where nn is the ObjectType whose retail resource is SHIP.Vnn or
 * SHIPTYPE.Vnn.  Each vertex is
 * x y z nx ny nz (float) r g b views (uchar).  The model frame is nose = -z,
 * up = +y, one unit = one sprite pixel at screen scale 0x100.  Next to each
 * PLY sits a .light sidecar made by bin/ship3dLight.py from the model's .npz.
 *
 * The game's own frames are right/down/forward with a 0x100 unit length (the
 * projection adds view-space y straight onto the screen row), so a model
 * point (x, y, z) is the game-frame point (x, -y, -z).  That is a proper
 * rotation, so no mirroring is involved.
 *
 * Relighting follows ship3d/README.md.  The sprites were shaded by a
 * headlight, and the delighting fitted that shading so that a point's colour
 * under any light L depends only on clip(n.L, 0, 1).  The sidecar holds that
 * dependence as each point's palette index at evenly spaced values of n.L,
 * whichever colour model made it.  The result is a palette index, not RGB,
 * so the game's own palette fades act on ships as they do on sprites.
 *
 * Two extra terms add detail the sprites never had.  Ambient occlusion is
 * worked out once per model from the hull's distance field (hullfield.c):
 * short probes over the hemisphere around each point's normal find how much
 * of it the hull itself closes off.  In the GL layer it scales the point's
 * colour; in the 8-bit frame it scales the point's place on its ramp.  Edge
 * lighting is a rim light from camera-space (+1, +1, +1), behind the ship to
 * the lower right, on surfaces turned edge-on to the viewer; with
 * --backdrop-light it comes from the backdrop's sun or lit planet instead
 * (backdroplight.c).  It moves the
 * point up its own ramp, so it stays in the ship's colours and still follows
 * palette fades.  A Fresnel term does the same, from no particular light,
 * on surfaces seen almost exactly edge-on, which outlines the hull faintly.
 *
 * With --ship3d-truecolour the GL layer skips the palette: a version 3
 * sidecar also holds the quantised-RGB model itself, each point's albedo and
 * the ship's kappa and c, and the point is drawn as
 * albedo * max(1 + kappa * (n.L - c), 0.05) without snapping to the nearest
 * palette entry.  Those colours do not follow palette fades.
 *
 * Points are splatted into the 8-bit space buffer with a per-ship depth
 * buffer.
 *
 * --rock3d draws the asteroids the same way, from ROCK_nn.ply where nn is the
 * OBJECTS.VGA packet of the tumble frames (16 for ASTEROID1/3/5 and the rock
 * chunks, 17 for ASTEROID2/4/6).  Those clouds were carved from one turn of
 * the tumble, so their frame is the camera frame of sprite frame 0 and their
 * version 4 sidecar holds a grey fitted light, the tumble axis and the turn
 * per frame.  The hue comes from the palette entries the rock's own sprite
 * uses: each point's shade table takes, at each n.L, the entry nearest in
 * GAME.PAL luminance to albedo * (ambient + diffuse * n.L).  A rock tumbles
 * about that axis at the rate and in the direction its animation steps
 * through the frames, interpolated between frames and across --interpolate's
 * in-between frames, and every rock slot gets its own fixed attitude and phase so the
 * field does not turn in step. */
#include "wc1.h"

#include "video_internal.h"

#define SHIP3D_TYPE_COUNT 64
#define SHIP3D_PLY_STRIDE 28
#define SHIP3D_MAX_VIEWPORT_PIXELS (640 * 480)

/* The cloud is the surface of a voxel grid with a pitch of exactly one unit,
 * and each point is a voxel centre.  Drawing every point as its voxel's
 * projected footprint (the screen bounding box of a unit cube, which depends
 * on orientation) covers the voxel shell's silhouette with no seams at any
 * range or angle.  The carved shells do have a few single-voxel gaps of their
 * own, which the exact footprint cannot cover; growing it by half again
 * closed them in every case measured (random orientations, scales 1-3, five
 * models) for about 7% more silhouette area. */
#define SHIP3D_FOOTPRINT 1.5f

#define SHIP3D_MAX_SHADE_STEPS 1024

/* Ambient occlusion: probe distances along each direction, in hull-field
 * cells, and how much of the light a fully occluded point loses. */
#define SHIP3D_AO_DIRECTIONS 9
#define SHIP3D_AO_PROBES 4
#define SHIP3D_AO_STRENGTH 0.41f

/* Edge lighting: the rim light's reach up the shade ramp and how tightly it
 * hugs the silhouette. */
#define SHIP3D_RIM_STRENGTH 0.4f
#define SHIP3D_RIM_POWER 2.85f

/* --glare silhouettes: how much of the camera light a ship lined up with
 * the sun loses, and how far up its ramp the sun's rim moves an edge. */
#define SHIP3D_SILHOUETTE_KEY 0.95f
#define SHIP3D_SILHOUETTE_RIM 2.4f
/* And how much darker the whole hull goes. */
#define SHIP3D_SILHOUETTE_DARKEN 0.8f

/* Fresnel outline: Schlick's (1 - n.v)^5 with no reflectance head-on, scaled
 * to how far up the shade ramp it can move a point seen edge-on. */
#define SHIP3D_FRESNEL_STRENGTH 0.57f

/* Floats per point handed to the GL layer: x, y, depth, low and high palette
 * entries, blend, brightness. */
#define SHIP3D_GL_POINT_FLOATS 7

typedef struct Ship3dPoint {
    float x;
    float y;
    float z;
    float nx;
    float ny;
    float nz;
} Ship3dPoint;

typedef struct Ship3dModel {
    Ship3dPoint *points;
    int count;
    int tried;
    int steps;
    /* shade[point * steps + step]: palette index at n.L = step / (steps - 1) */
    unsigned char *shade;
    /* Version 3 sidecars: albedo[point * 3 + channel], RGB 0..1, and the
     * shading strength and reference n.L of the quantised-RGB model.
     * unlit[point] is nonzero where no sprite saw the point; its albedo is
     * then its stored colour, drawn under any light. */
    float *albedo;
    unsigned char *unlit;
    float kappa;
    float reference;
    /* occlusion[point]: brightness 0..1 left by ambient occlusion */
    float *occlusion;
    int occlusionTried;
    /* The farthest point from the origin, for --glare; 0 until measured. */
    float radius;
    /* Version 4 (rock) sidecars: grey albedo per point, the fitted light,
     * the tumble axis and radians per frame.  shade stays NULL until the
     * rock's sprite has given it its colours; rockShadeTried is set then. */
    float *rockAlbedo;
    float rockAmbient;
    float rockDiffuse;
    float rockAxis[3];
    float rockStep;
    int rockShadeTried;
} Ship3dModel;

static char g_szShip3dDirectory[1024];
static int g_bShip3dEnabled;
static int g_bRock3dEnabled;
static int g_bShip3dTrueColour;
static Ship3dModel g_aShip3dModels[SHIP3D_TYPE_COUNT];
static float *g_pfShip3dDepth;
static int g_nShip3dDepthPixels;
static float *g_pfShip3dGlPoints;
static int g_nShip3dGlPointCapacity;
/* Where each object's orientation comes from: SHIP3D_POSE_*. */
static unsigned char g_aeShip3dPoseSource[SPACE_OBJECT_COUNT];

/* Where the models are read from, for ships and rocks alike.  Resolved now,
 * since the game later changes into GAMEDAT. */
void SdlShip3dSetDirectory(const char *directory)
{
    const char *path;

    path = directory != NULL && directory[0] != '\0' ? directory : "ship3d";
#ifndef _WIN32
    if (realpath(path, g_szShip3dDirectory) == NULL)
        SDL_strlcpy(g_szShip3dDirectory, path, sizeof(g_szShip3dDirectory));
#else
    SDL_strlcpy(g_szShip3dDirectory, path, sizeof(g_szShip3dDirectory));
#endif
}

void SdlShip3dEnable(const char *directory)
{
    SdlShip3dSetDirectory(directory);
    g_bShip3dEnabled = 1;
}

static int Ship3dFileExists(const char *directory, const char *name, int type)
{
    char path[1200];
    SDL_RWops *file;

    SDL_snprintf(path, sizeof(path), "%s/%s%02d.light", directory, name, type);
    file = SDL_RWFromFile(path, "rb");
    if (file == NULL) {
        SDL_ClearError();
        return 0;
    }
    SDL_RWclose(file);
    return 1;
}

/* Nonzero when directory holds relit models: a ship's or, with rocks, an
 * asteroid's .light sidecar.  Lets --enhanced turn the models on only where
 * bin/ship3dLight.py has been run. */
int SdlShip3dHasModels(const char *directory, int rocks)
{
    int type;

    if (rocks)
        return Ship3dFileExists(directory, "ROCK_", 16);
    for (type = 0; type < SHIP3D_TYPE_COUNT; type++) {
        if (Ship3dFileExists(directory, "SHIP_V", type) ||
            Ship3dFileExists(directory, "SHIPTYPE_V", type))
            return 1;
    }
    return 0;
}

/* --rock3d on its own reads the models from ./ship3d. */
void SdlRock3dEnable(void)
{
    g_bRock3dEnabled = 1;
}

/* Nonzero when any objects are drawn from point clouds. */
int SdlShip3dEnabled(void)
{
    return g_bShip3dEnabled || g_bRock3dEnabled;
}

void SdlShip3dEnableTrueColour(void)
{
    g_bShip3dTrueColour = 1;
}

/* With the GL renderer the ships are drawn at output resolution in the
 * object layer.  The hit-sphere overlay and the EGA filter work on the
 * indexed frame, so they keep the ships there.  The shield flash follows
 * the ships into the GL layer. */
int SdlShip3dUsesGl(void)
{
    return SdlShip3dEnabled() && SdlUsingGlRenderer() &&
           !SdlHitSphereEnabled() && !SdlEgaDitherEnabled();
}

static unsigned char *ReadShip3dFile(const char *path, size_t *length)
{
    SDL_RWops *file;
    Sint64 size;
    unsigned char *data;

    file = SDL_RWFromFile(path, "rb");
    if (file == NULL)
        return NULL;
    size = SDL_RWsize(file);
    if (size <= 0 || size > (Sint64)64 * 1024 * 1024) {
        SDL_RWclose(file);
        return NULL;
    }
    data = SDL_malloc((size_t)size + 1);
    if (data == NULL || SDL_RWread(file, data, 1, (size_t)size) != (size_t)size) {
        SDL_RWclose(file);
        SDL_free(data);
        return NULL;
    }
    SDL_RWclose(file);
    data[size] = 0;
    *length = (size_t)size;
    return data;
}

static unsigned int ReadShip3dWord(const unsigned char *at)
{
    return (unsigned int)at[0] | (unsigned int)at[1] << 8 |
           (unsigned int)at[2] << 16 | (unsigned int)at[3] << 24;
}

static float ReadShip3dFloat(const unsigned char *at)
{
    float value;

    SDL_memcpy(&value, at, sizeof(value));
    return value;
}

/* The extent [*start, *end] of the run of equal entries holding step. */
static void GetShip3dRun(const unsigned char *shade, int steps, int step,
                         int *start, int *end)
{
    *start = step;
    while (*start > 0 && shade[*start - 1] == shade[step])
        (*start)--;
    *end = step;
    while (*end + 1 < steps && shade[*end + 1] == shade[step])
        (*end)++;
}

/* Each run of equal entries stands for one colour at the run's centre.  A
 * run at an end of the table is cut off by n.L reaching 0 or 1, so its
 * centre is pulled in to a neighbour's width from its inner boundary. */
static float GetShip3dRunCentre(const unsigned char *shade, int steps,
                                int start, int end)
{
    float centre = (float)(start + end) * 0.5f;
    int otherStart;
    int otherEnd;
    float pulled;

    if (start == 0 && end + 1 < steps) {
        GetShip3dRun(shade, steps, end + 1, &otherStart, &otherEnd);
        pulled = (float)end + 0.5f - (float)(otherEnd - otherStart + 1) * 0.5f;
        if (pulled > centre)
            centre = pulled;
    } else if (end == steps - 1 && start > 0) {
        GetShip3dRun(shade, steps, start - 1, &otherStart, &otherEnd);
        pulled = (float)start - 0.5f + (float)(otherEnd - otherStart + 1) * 0.5f;
        if (pulled < centre)
            centre = pulled;
    }
    return centre;
}

/* The shade table is a staircase in n.L.  For the GL layer, between the
 * centres of two neighbouring runs blend their palette entries: out gets the
 * entry below, the entry above and how far (0..1) position is between them. */
static void GetShip3dBlend(const unsigned char *shade, int steps,
                           float position, float out[3])
{
    int step = (int)(position + 0.5f);
    int start;
    int end;
    int otherStart;
    int otherEnd;
    float centre;
    float otherCentre;

    GetShip3dRun(shade, steps, step, &start, &end);
    centre = GetShip3dRunCentre(shade, steps, start, end);
    out[0] = (float)shade[step];
    out[1] = out[0];
    out[2] = 0.0f;
    if (position > centre && end + 1 < steps) {
        GetShip3dRun(shade, steps, end + 1, &otherStart, &otherEnd);
        otherCentre = GetShip3dRunCentre(shade, steps, otherStart, otherEnd);
        out[1] = (float)shade[end + 1];
        out[2] = (position - centre) / (otherCentre - centre);
    } else if (position < centre && start > 0) {
        GetShip3dRun(shade, steps, start - 1, &otherStart, &otherEnd);
        otherCentre = GetShip3dRunCentre(shade, steps, otherStart, otherEnd);
        out[0] = (float)shade[start - 1];
        out[1] = (float)shade[step];
        out[2] = (position - otherCentre) / (centre - otherCentre);
    }
    if (out[2] < 0.0f)
        out[2] = 0.0f;
    else if (out[2] > 1.0f)
        out[2] = 1.0f;
}

/* Fills the shade tables, and from a version 3 sidecar the albedo, from the
 * .light sidecar.  plyColours holds each point's stored colour, at the PLY
 * record stride. */
static int LoadShip3dLight(Ship3dModel *model, const char *plyPath,
                           const unsigned char *plyColours)
{
    char path[1200];
    unsigned char *data;
    size_t size;
    size_t length;
    size_t header;
    size_t expected;
    unsigned int version;
    unsigned int steps;

    length = SDL_strlen(plyPath);
    if (length < 4 || length >= sizeof(path))
        return 0;
    SDL_memcpy(path, plyPath, length - 3);
    SDL_strlcpy(path + length - 3, "light", sizeof(path) - (length - 3));
    data = ReadShip3dFile(path, &size);
    if (data == NULL) {
        fprintf(stderr,
                "ship3d: %s is missing; run bin/ship3dLight.py to make it\n",
                path);
        return 0;
    }
    version = size >= 16 ? ReadShip3dWord(data + 4) : 0;
    steps = size >= 16 ? ReadShip3dWord(data + 12) : 0;
    if (version == 4) {
        /* A rock: the shade tables wait for its sprite's colours. */
        int index;

        if (SDL_memcmp(data, "S3DL", 4) != 0 ||
            ReadShip3dWord(data + 8) != (unsigned int)model->count ||
            steps < 2 || steps > SHIP3D_MAX_SHADE_STEPS ||
            size != 40 + (size_t)model->count * sizeof(float)) {
            fprintf(stderr,
                    "ship3d: %s does not match its model; rerun "
                    "bin/ship3dLight.py\n",
                    path);
            SDL_free(data);
            return 0;
        }
        model->rockAlbedo = SDL_malloc((size_t)model->count * sizeof(float));
        if (model->rockAlbedo == NULL) {
            SDL_free(data);
            return 0;
        }
        model->steps = (int)steps;
        model->rockAmbient = ReadShip3dFloat(data + 16);
        model->rockDiffuse = ReadShip3dFloat(data + 20);
        model->rockAxis[0] = ReadShip3dFloat(data + 24);
        model->rockAxis[1] = ReadShip3dFloat(data + 28);
        model->rockAxis[2] = ReadShip3dFloat(data + 32);
        model->rockStep = ReadShip3dFloat(data + 36);
        for (index = 0; index < model->count; index++)
            model->rockAlbedo[index] =
                ReadShip3dFloat(data + 40 + (size_t)index * 4);
        SDL_free(data);
        return 1;
    }
    header = version == 3 ? 24 : 16;
    expected = header + (size_t)model->count * steps;
    if (version == 3)
        expected += (size_t)model->count * 3 * sizeof(float);
    if (size < 16 || SDL_memcmp(data, "S3DL", 4) != 0 ||
        (version != 2 && version != 3) ||
        ReadShip3dWord(data + 8) != (unsigned int)model->count ||
        steps < 2 || steps > SHIP3D_MAX_SHADE_STEPS || size != expected) {
        fprintf(stderr,
                "ship3d: %s does not match its model; rerun "
                "bin/ship3dLight.py\n",
                path);
        SDL_free(data);
        return 0;
    }
    model->shade = SDL_malloc((size_t)model->count * steps);
    if (model->shade == NULL) {
        SDL_free(data);
        return 0;
    }
    SDL_memcpy(model->shade, data + header, (size_t)model->count * steps);
    model->steps = (int)steps;
    if (version == 3) {
        const unsigned char *albedo =
            data + header + (size_t)model->count * steps;
        int index;

        model->kappa = ReadShip3dFloat(data + 16);
        model->reference = ReadShip3dFloat(data + 20);
        model->albedo = SDL_malloc((size_t)model->count * 3 * sizeof(float));
        model->unlit = SDL_calloc((size_t)model->count, 1);
        if (model->albedo == NULL || model->unlit == NULL) {
            SDL_free(model->albedo);
            SDL_free(model->unlit);
            model->albedo = NULL;
            model->unlit = NULL;
        }
        for (index = 0; model->albedo != NULL && index < model->count;
             index++) {
            const unsigned char *stored =
                plyColours + (size_t)index * SHIP3D_PLY_STRIDE;
            float *out = model->albedo + (size_t)index * 3;
            int channel;

            if (ReadShip3dFloat(albedo + (size_t)index * 12) < 0.0f) {
                model->unlit[index] = 1;
                for (channel = 0; channel < 3; channel++)
                    out[channel] = (float)stored[channel] / 255.0f;
                continue;
            }
            for (channel = 0; channel < 3; channel++)
                out[channel] = ReadShip3dFloat(
                    albedo + (size_t)index * 12 + (size_t)channel * 4) /
                    255.0f;
        }
    }
    SDL_free(data);
    return 1;
}

/* Fraction (0..1) of the hemisphere above point (x, y, z) with unit normal n
 * that the hull closes off.  Each probe compares the distance to the hull
 * with what a flat surface would leave free there. */
static float MeasureShip3dOcclusion(const HullField *field, float cell,
                                    const float *point, const float *normal)
{
    static const float probeCells[SHIP3D_AO_PROBES] = {
        1.5f, 3.0f, 6.0f, 12.0f
    };
    static const float probeWeight[SHIP3D_AO_PROBES] = {
        1.0f, 0.8f, 0.6f, 0.4f
    };
    float tangent[3];
    float bitangent[3];
    float length;
    float occlusion = 0.0f;
    float total = 0.0f;
    int direction;

    /* Any unit vector at right angles to the normal, then a third. */
    if (SDL_fabsf(normal[0]) < 0.9f) {
        tangent[0] = 0.0f;
        tangent[1] = normal[2];
        tangent[2] = -normal[1];
    } else {
        tangent[0] = -normal[2];
        tangent[1] = 0.0f;
        tangent[2] = normal[0];
    }
    length = SDL_sqrtf(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                       tangent[2] * tangent[2]);
    if (length <= 0.0f)
        return 0.0f;
    tangent[0] /= length;
    tangent[1] /= length;
    tangent[2] /= length;
    bitangent[0] = normal[1] * tangent[2] - normal[2] * tangent[1];
    bitangent[1] = normal[2] * tangent[0] - normal[0] * tangent[2];
    bitangent[2] = normal[0] * tangent[1] - normal[1] * tangent[0];

    /* The normal, then eight directions 50 degrees off it. */
    for (direction = 0; direction < SHIP3D_AO_DIRECTIONS; direction++) {
        float along = direction == 0 ? 1.0f : 0.643f;
        float across = direction == 0 ? 0.0f : 0.766f;
        float angle = (float)direction * (2.0f * 3.14159265f / 8.0f);
        float cosine = SDL_cosf(angle) * across;
        float sine = SDL_sinf(angle) * across;
        float ray[3];
        int probe;

        ray[0] = normal[0] * along + tangent[0] * cosine + bitangent[0] * sine;
        ray[1] = normal[1] * along + tangent[1] * cosine + bitangent[1] * sine;
        ray[2] = normal[2] * along + tangent[2] * cosine + bitangent[2] * sine;
        for (probe = 0; probe < SHIP3D_AO_PROBES; probe++) {
            float reach = probeCells[probe] * cell;
            /* Start from the surface, half a voxel out from the centre. */
            float distance = SdlSampleHullField(
                field, point[0] + normal[0] * 0.5f + ray[0] * reach,
                point[1] + normal[1] * 0.5f + ray[1] * reach,
                point[2] + normal[2] * 0.5f + ray[2] * reach);
            float open = reach * along;
            float closed = (open - distance) / open;

            if (closed < 0.0f)
                closed = 0.0f;
            else if (closed > 1.0f)
                closed = 1.0f;
            occlusion += probeWeight[probe] * along * closed;
            total += probeWeight[probe] * along;
        }
    }
    return total > 0.0f ? occlusion / total : 0.0f;
}

/* Fills model->occlusion once, from the hull's distance field.  Without a
 * field every point keeps full brightness. */
static const float *GetShip3dOcclusion(Ship3dModel *model, int type)
{
    const HullField *field;
    float cell;
    float sum = 0.0f;
    int index;

    if (model->occlusionTried)
        return model->occlusion;
    model->occlusionTried = 1;
    field = SdlGetHullField(type);
    if (field == NULL)
        return NULL;
    model->occlusion = SDL_malloc((size_t)model->count * sizeof(float));
    if (model->occlusion == NULL)
        return NULL;
    cell = SdlGetHullFieldCell(field);
    for (index = 0; index < model->count; index++) {
        const Ship3dPoint *point = &model->points[index];
        float brightness = 1.0f - SHIP3D_AO_STRENGTH *
            MeasureShip3dOcclusion(field, cell, &point->x, &point->nx);

        model->occlusion[index] = brightness;
        sum += brightness;
    }
    fprintf(stderr, "ship3d: type %d ambient occlusion, mean brightness %.2f\n",
            type, sum / (float)model->count);
    return model->occlusion;
}

/* Towards the rim light in camera space, unit length: (+1, +1, +1) unless
 * the backdrop sets it for this frame. */
static float g_afShip3dRimLight[3] = { 0.57735027f, 0.57735027f,
                                       0.57735027f };

/* How far up its ramp the rim light moves a point with camera-space normal
 * (x, y, z). */
static float GetShip3dRim(float normalX, float normalY, float normalZ)
{
    float edge = 1.0f + normalZ; /* 1 - n.v, the viewer at -z */
    float facing = normalX * g_afShip3dRimLight[0] +
                   normalY * g_afShip3dRimLight[1] +
                   normalZ * g_afShip3dRimLight[2];

    if (edge > 1.0f)
        edge = 1.0f;
    if (edge <= 0.0f || facing <= 0.0f)
        return 0.0f;
    return SHIP3D_RIM_STRENGTH * SDL_powf(edge, SHIP3D_RIM_POWER) * facing;
}

/* How far up its ramp the Fresnel term moves a point with camera-space
 * normal z; it is only strong within a few degrees of edge-on. */
static float GetShip3dFresnel(float normalZ)
{
    float edge = 1.0f + normalZ; /* 1 - n.v, the viewer at -z */
    float squared;

    if (edge <= 0.0f)
        return 0.0f;
    if (edge > 1.0f)
        edge = 1.0f;
    squared = edge * edge;
    return SHIP3D_FRESNEL_STRENGTH * squared * squared * edge;
}

/* The OBJECTS.VGA packet whose tumble frames type draws, or 0 if it is not
 * a rock.  load_ship gives ASTEROID1/3/5 packet 16 and ASTEROID2/4/6 packet
 * 17; set_objects_data turns 2/4/6 into 1/3/5 when packet 17 is not loaded,
 * and draws the rock chunks with ASTEROID1's frames. */
static int GetRock3dPacket(int type)
{
    switch (type) {
    case OBJECT_TYPE_ASTEROID1:
    case OBJECT_TYPE_ASTEROID3:
    case OBJECT_TYPE_ASTEROID5:
    case OBJECT_TYPE_ROCK_CHUNK:
        return 16;
    case OBJECT_TYPE_ASTEROID2:
    case OBJECT_TYPE_ASTEROID4:
    case OBJECT_TYPE_ASTEROID6:
        return 17;
    }
    return 0;
}

static void LoadShip3dModel(int type)
{
    Ship3dModel *model;
    char path[1200];
    unsigned char *data;
    size_t size;
    const char *header;
    const char *body;
    int count;
    int index;

    model = &g_aShip3dModels[type];
    model->tried = 1;
    /* Retail keeps capital ships in SHIP.Vnn and fighters (and the Spikeri)
     * in SHIPTYPE.Vnn; the models follow the same split. */
    if (GetRock3dPacket(type) != 0)
        SDL_snprintf(path, sizeof(path), "%s/ROCK_%02d.ply",
                     g_szShip3dDirectory, GetRock3dPacket(type));
    else
        SDL_snprintf(path, sizeof(path), "%s/SHIP_V%02d.ply",
                     g_szShip3dDirectory, type);
    data = ReadShip3dFile(path, &size);
    if (data == NULL && GetRock3dPacket(type) == 0) {
        SDL_snprintf(path, sizeof(path), "%s/SHIPTYPE_V%02d.ply",
                     g_szShip3dDirectory, type);
        data = ReadShip3dFile(path, &size);
    }
    if (data == NULL)
        return;

    header = (const char *)data;
    body = SDL_strstr(header, "end_header\n");
    if (body == NULL || SDL_strstr(header, "binary_little_endian") == NULL) {
        SDL_free(data);
        return;
    }
    body += 11;
    count = 0;
    {
        const char *element = SDL_strstr(header, "element vertex ");

        if (element != NULL)
            count = SDL_atoi(element + 15);
    }
    if (count <= 0 ||
        (Sint64)(body - header) + (Sint64)count * SHIP3D_PLY_STRIDE >
            (Sint64)size) {
        SDL_free(data);
        return;
    }
    model->points = SDL_calloc((size_t)count, sizeof(Ship3dPoint));
    if (model->points == NULL) {
        SDL_free(data);
        return;
    }
    for (index = 0; index < count; index++) {
        const unsigned char *record =
            (const unsigned char *)body + (size_t)index * SHIP3D_PLY_STRIDE;
        Ship3dPoint *point = &model->points[index];

        SDL_memcpy(&point->x, record, 24);
    }
    model->count = count;
    if (!LoadShip3dLight(model, path, (const unsigned char *)body + 24)) {
        SDL_free(data);
        SDL_free(model->points);
        model->points = NULL;
        model->count = 0;
        return;
    }
    SDL_free(data);
    fprintf(stderr, "ship3d: loaded %d points from %s\n", count, path);
}

static const Ship3dModel *GetShip3dModel(int type)
{
    Ship3dModel *model;

    if (type < 0 || type >= SHIP3D_TYPE_COUNT)
        return NULL;
    model = &g_aShip3dModels[type];
    if (!model->tried) {
        if (g_szShip3dDirectory[0] == '\0') {
#ifndef _WIN32
            if (realpath("ship3d", g_szShip3dDirectory) == NULL)
#endif
                SDL_strlcpy(g_szShip3dDirectory, "ship3d",
                            sizeof(g_szShip3dDirectory));
        }
        LoadShip3dModel(type);
    }
    return model->count > 0 ? model : NULL;
}

/* The pixels whose centres fall within half of centre; a footprint narrower
 * than a pixel still lights the pixel that holds its centre. */
static void GetShip3dSpan(float centre, float half, int *first, int *last)
{
    *first = (int)SDL_ceilf(centre - half - 0.5f);
    *last = (int)SDL_floorf(centre + half - 0.5f);
    if (*last < *first) {
        *first = (int)SDL_floorf(centre);
        *last = *first;
    }
}

static float DotShip3d(const FixedVector *a, const FixedVector *b)
{
    return ((float)a->x * (float)b->x + (float)a->y * (float)b->y +
            (float)a->z * (float)b->z) * (1.0f / 65536.0f);
}

void SdlShip3dSetPoseSource(short obj, int source)
{
    if (obj >= 0 && obj < SPACE_OBJECT_COUNT)
        g_aeShip3dPoseSource[obj] = (unsigned char)source;
}

/* The model-to-camera rotation that the sprite view obj currently shows
 * (its frame, flip and screen angle) stands for.  This undoes
 * get_right_shape: the frame and flip name one of the 62 view directions,
 * whose right/up/forward vectors are the camera's axes in the ship's own
 * frame before the sprite is turned by the screen angle.  Over random
 * orientations the result stays within 26 degrees of the orientation
 * get_right_shape was given, which is the coarseness of the 30-degree views.
 * Returns 0 if the frame is not one of the fighter views. */
static int ComputeShip3dSpriteBasis(short obj, float basis[3][3])
{
    const float sign[3] = { 1.0f, -1.0f, -1.0f };
    float right[3];
    float up[3];
    float forward[3];
    float camera[3][3];
    float radians;
    float cosine;
    float sine;
    int frame = asObjectViewFrames[obj];
    int flip = (asObjectFlipArray[obj] >> 4) & 3;
    int angle = asObjScreenAngles[obj];
    int direction;
    int axis;

    for (direction = 0; direction < DIRECTION_VIEW_COUNT; direction++) {
        if (acDirectionShapeFrame[direction] == frame &&
            acDirectionShapeFlip[direction] == flip)
            break;
    }
    if (direction == DIRECTION_VIEW_COUNT)
        return 0;
    /* get_right_shape turns the two pole views a further quarter. */
    if (frame == 0)
        angle -= 90;
    else if (frame == 36)
        angle += 90;

    right[0] = (float)aDirectionViewRightVector[direction].x / 256.0f;
    right[1] = (float)aDirectionViewRightVector[direction].y / 256.0f;
    right[2] = (float)aDirectionViewRightVector[direction].z / 256.0f;
    up[0] = (float)aDirectionViewUpVectorArray[direction].x / 256.0f;
    up[1] = (float)aDirectionViewUpVectorArray[direction].y / 256.0f;
    up[2] = (float)aDirectionViewUpVectorArray[direction].z / 256.0f;
    forward[0] = (float)aDirectionViewForwardVector[direction].x / 256.0f;
    forward[1] = (float)aDirectionViewForwardVector[direction].y / 256.0f;
    forward[2] = (float)aDirectionViewForwardVector[direction].z / 256.0f;

    radians = (float)angle * (3.14159265358979f / 180.0f);
    cosine = SDL_cosf(radians);
    sine = SDL_sinf(radians);
    for (axis = 0; axis < 3; axis++) {
        camera[1][axis] = cosine * up[axis] - sine * right[axis];
        /* The table's forward vector points from the ship to the viewer. */
        camera[2][axis] = -forward[axis];
    }
    camera[0][0] = camera[1][1] * camera[2][2] - camera[1][2] * camera[2][1];
    camera[0][1] = camera[1][2] * camera[2][0] - camera[1][0] * camera[2][2];
    camera[0][2] = camera[1][0] * camera[2][1] - camera[1][1] * camera[2][0];
    for (axis = 0; axis < 3; axis++) {
        basis[0][axis] = sign[axis] * camera[0][axis];
        basis[1][axis] = sign[axis] * camera[1][axis];
        basis[2][axis] = sign[axis] * camera[2][axis];
    }
    return 1;
}

/* The model-to-camera rotation for obj: basis[camera][modelAxis]. */
void SdlShip3dComputeBasis(short obj, float basis[3][3])
{
    const FixedVector *eye[3];
    const FixedVector *object[3];
    float sign[3] = { 1.0f, -1.0f, -1.0f };
    int axis;

    if (g_aeShip3dPoseSource[obj] == SHIP3D_POSE_SPRITE_VIEW &&
        ComputeShip3dSpriteBasis(obj, basis))
        return;
    eye[0] = &aShipRightVector[EYE_OBJECT];
    eye[1] = &aShipUpVector[EYE_OBJECT];
    eye[2] = &aShipFwdVec[EYE_OBJECT];
    object[0] = &aShipRightVector[obj];
    object[1] = &aShipUpVector[obj];
    object[2] = &aShipFwdVec[obj];
    for (axis = 0; axis < 3; axis++) {
        int camera;

        for (camera = 0; camera < 3; camera++)
            basis[camera][axis] =
                sign[axis] * DotShip3d(object[axis], eye[camera]);
    }
}

int SdlShip3dGetPoints(int type, const float **xyz, int *stride, int *count)
{
    const Ship3dModel *model;

    model = GetShip3dModel(type);
    if (model == NULL)
        return 0;
    *xyz = &model->points[0].x;
    *stride = (int)(sizeof(Ship3dPoint) / sizeof(float));
    *count = model->count;
    return 1;
}

static float GetShip3dRadius(Ship3dModel *model)
{
    int index;

    if (model->radius <= 0.0f) {
        float squared = 0.0f;

        for (index = 0; index < model->count; index++) {
            const Ship3dPoint *point = &model->points[index];
            float length = point->x * point->x + point->y * point->y +
                           point->z * point->z;

            if (length > squared)
                squared = length;
        }
        model->radius = squared > 0.0f ? SDL_sqrtf(squared) : 1.0f;
    }
    return model->radius;
}

/* With --glare, for a ship lined up with the sun: how far the sun's rim
 * moves a point with camera-space normal (x, y, z) up its ramp. */
static float GetShip3dSunRim(float silhouette, const float sun[3],
                             float normalX, float normalY, float normalZ)
{
    float edge = 1.0f + normalZ;
    float facing;

    if (silhouette <= 0.0f)
        return 0.0f;
    if (edge > 1.0f)
        edge = 1.0f;
    facing = normalX * sun[0] + normalY * sun[1] + normalZ * sun[2] + 0.35f;
    if (edge <= 0.0f || facing <= 0.0f)
        return 0.0f;
    return silhouette * SHIP3D_SILHOUETTE_RIM * SDL_powf(edge, 1.8f) * facing;
}

/* The largest tumble frame read for its colours. */
#define ROCK3D_MAX_SPRITE_PIXELS (256 * 256)
/* Entries covering less than 1/ROCK3D_RARE_ENTRY of the sprite's pixels are
 * strays, not part of the rock's ramp. */
#define ROCK3D_RARE_ENTRY 500
/* Luminance lookup: quarter steps from 0 to 511.75. */
#define ROCK3D_LUM_STEPS 2048

/* For a rock drawn as a point cloud, the model slot it uses (ASTEROID1 for
 * packet 16, ASTEROID2 for packet 17); otherwise -1. */
static int GetRock3dModelType(short obj)
{
    int packet;

    if (!g_bRock3dEnabled || SdlSwcFlightActive())
        return -1;
    if (aeObjectClass[obj] != OBJECT_CLASS_ASTEROID &&
        aeObjectTypeList[obj] != OBJECT_TYPE_ROCK_CHUNK)
        return -1;
    packet = GetRock3dPacket(aeObjectTypeList[obj]);
    if (packet == 16)
        return OBJECT_TYPE_ASTEROID1;
    if (packet == 17)
        return OBJECT_TYPE_ASTEROID2;
    return -1;
}

/* Fills a rock's shade tables once, from the palette entries its tumble
 * frames use and their luminance in GAME.PAL.  The tables hold palette
 * indices, so palette fades act on the rock as on its sprite.  Returns 0 if
 * the colours cannot be found. */
static int BuildRock3dShade(Ship3dModel *model, struct Shape *shape)
{
    static unsigned char pixels[ROCK3D_MAX_SPRITE_PIXELS];
    static const float weight[3] = { 0.30f, 0.59f, 0.11f };
    char resolved[1024];
    unsigned char *palette;
    unsigned long counts[256];
    unsigned long total = 0;
    unsigned char lut[ROCK3D_LUM_STEPS];
    float luminance[256];
    int used[256];
    int usedCount = 0;
    size_t size = 0;
    short frames;
    short frame;
    int index;
    int step;

    if (model->shade != NULL)
        return 1;
    if (model->rockShadeTried || model->rockAlbedo == NULL || shape == NULL)
        return 0;
    model->rockShadeTried = 1;
    palette = NULL;
    if (SdlResolvePath("game.pal", resolved, sizeof(resolved)))
        palette = ReadShip3dFile(resolved, &size);
    if (palette == NULL || size < 0x30 + 0x300) {
        fprintf(stderr, "ship3d: no GAME.PAL to colour the rocks\n");
        SDL_free(palette);
        return 0;
    }

    SDL_zero(counts);
    frames = GetShapeFrameCount(shape);
    for (frame = 0; frame < frames; frame++) {
        short width = 0;
        short height = 0;
        short originX = 0;
        short originY = 0;
        int pixel;

        GetShapeFrameExtents(shape, frame, &width, &height, &originX,
                             &originY);
        if (width <= 0 || height <= 0 ||
            (int)width * height > ROCK3D_MAX_SPRITE_PIXELS)
            continue;
        SDL_memset(pixels, 0xff, (size_t)width * height);
        DecodeShapeFrame(shape, frame, pixels, width, height, originX,
                         originY);
        for (pixel = 0; pixel < (int)width * height; pixel++) {
            if (pixels[pixel] != 0 && pixels[pixel] != 0xff) {
                counts[pixels[pixel]]++;
                total++;
            }
        }
    }
    for (index = 0; index < 256; index++) {
        const unsigned char *rgb = palette + 0x30 + index * 3;

        luminance[index] = weight[0] * rgb[0] + weight[1] * rgb[1] +
                           weight[2] * rgb[2];
        if (counts[index] > 0 &&
            counts[index] * ROCK3D_RARE_ENTRY >= total)
            used[usedCount++] = index;
    }
    SDL_free(palette);
    if (usedCount == 0) {
        fprintf(stderr, "ship3d: the rock's sprite has no colours\n");
        return 0;
    }

    /* The used entry nearest in luminance at each quarter step. */
    for (step = 0; step < ROCK3D_LUM_STEPS; step++) {
        float target = (float)step * 0.25f;
        int best = used[0];
        int entry;

        for (entry = 1; entry < usedCount; entry++) {
            if (SDL_fabsf(luminance[used[entry]] - target) <
                SDL_fabsf(luminance[best] - target))
                best = used[entry];
        }
        lut[step] = (unsigned char)best;
    }
    model->shade = SDL_malloc((size_t)model->count * model->steps);
    if (model->shade == NULL)
        return 0;
    for (index = 0; index < model->count; index++) {
        for (step = 0; step < model->steps; step++) {
            float lum = model->rockAlbedo[index] *
                        (model->rockAmbient + model->rockDiffuse *
                                                  (float)step /
                                                  (float)(model->steps - 1));
            int at = (int)(lum * 4.0f + 0.5f);

            if (at < 0)
                at = 0;
            else if (at >= ROCK3D_LUM_STEPS)
                at = ROCK3D_LUM_STEPS - 1;
            model->shade[(size_t)index * model->steps + step] = lut[at];
        }
    }
    fprintf(stderr, "ship3d: rock coloured from %d palette entries\n",
            usedCount);
    return 1;
}

/* out = a * b for 3x3 matrices. */
static void MultiplyRock3dMatrix(const float a[3][3], const float b[3][3],
                                 float out[3][3])
{
    int row;
    int column;

    for (row = 0; row < 3; row++)
        for (column = 0; column < 3; column++)
            out[row][column] = a[row][0] * b[0][column] +
                               a[row][1] * b[1][column] +
                               a[row][2] * b[2][column];
}

/* The model-to-camera rotation for the rock obj: the tumble about the
 * model's axis, then the slot's own attitude, then the object's frame. */
static void ComputeRock3dBasis(short obj, const Ship3dModel *model,
                               float basis[3][3])
{
    const ObjectTypeData *typeData = &aObjectTypeData[aeObjectTypeList[obj]];
    const unsigned int *animation =
        (const unsigned int *)typeData->animation;
    Uint32 state = 0x9e3779b9U * (Uint32)(obj + 1);
    float random[4];
    float quaternion[4];
    float attitude[3][3];
    float spin[3][3];
    float world[3][3];
    float combined[3][3];
    float angle;
    float progress;
    float cosine;
    float sine;
    float rootA;
    float rootB;
    const float *axis = model->rockAxis;
    int period = typeData->yawRate > 0 ? typeData->yawRate : 1;
    int elapsed = period - asObjectAnimationDelay[obj];
    int part;

    /* A generator of its own: the game's would change what the sim rolls. */
    for (part = 0; part < 4; part++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        random[part] = (float)(state >> 8) / 16777216.0f;
    }
    /* A uniformly random attitude (Shoemake's method). */
    rootA = SDL_sqrtf(1.0f - random[0]);
    rootB = SDL_sqrtf(random[0]);
    quaternion[0] = rootA * SDL_sinf(6.2831853f * random[1]);
    quaternion[1] = rootA * SDL_cosf(6.2831853f * random[1]);
    quaternion[2] = rootB * SDL_sinf(6.2831853f * random[2]);
    quaternion[3] = rootB * SDL_cosf(6.2831853f * random[2]);
    {
        float x = quaternion[0];
        float y = quaternion[1];
        float z = quaternion[2];
        float w = quaternion[3];

        attitude[0][0] = 1.0f - 2.0f * (y * y + z * z);
        attitude[0][1] = 2.0f * (x * y - z * w);
        attitude[0][2] = 2.0f * (x * z + y * w);
        attitude[1][0] = 2.0f * (x * y + z * w);
        attitude[1][1] = 1.0f - 2.0f * (x * x + z * z);
        attitude[1][2] = 2.0f * (y * z - x * w);
        attitude[2][0] = 2.0f * (x * z - y * w);
        attitude[2][1] = 2.0f * (y * z + x * w);
        attitude[2][2] = 1.0f - 2.0f * (x * x + y * y);
    }

    /* animate_shape shows a new frame every `period` ticks; the reverse
     * tables count down. */
    if (elapsed < 0)
        elapsed = 0;
    else if (elapsed > period - 1)
        elapsed = period - 1;
    progress = ((float)elapsed + (float)SdlFlightFrameFraction()) / (float)period;
    if (animation != NULL && animation[0] > animation[1])
        progress = -progress;
    angle = ((float)asObjectViewFrames[obj] + progress) * model->rockStep +
            6.2831853f * random[3];

    /* Rodrigues: rotation by angle about the unit axis. */
    cosine = SDL_cosf(angle);
    sine = SDL_sinf(angle);
    spin[0][0] = cosine + axis[0] * axis[0] * (1.0f - cosine);
    spin[0][1] = axis[0] * axis[1] * (1.0f - cosine) - axis[2] * sine;
    spin[0][2] = axis[0] * axis[2] * (1.0f - cosine) + axis[1] * sine;
    spin[1][0] = axis[1] * axis[0] * (1.0f - cosine) + axis[2] * sine;
    spin[1][1] = cosine + axis[1] * axis[1] * (1.0f - cosine);
    spin[1][2] = axis[1] * axis[2] * (1.0f - cosine) - axis[0] * sine;
    spin[2][0] = axis[2] * axis[0] * (1.0f - cosine) - axis[1] * sine;
    spin[2][1] = axis[2] * axis[1] * (1.0f - cosine) + axis[0] * sine;
    spin[2][2] = cosine + axis[2] * axis[2] * (1.0f - cosine);

    SdlShip3dComputeBasis(obj, world);
    MultiplyRock3dMatrix(world, attitude, combined);
    MultiplyRock3dMatrix(combined, spin, basis);
}

int SdlShip3dDraw(Viewport *viewport, short obj, float centerX, float centerY)
{
    Ship3dModel *model;
    const float *occlusion;
    RasterSurface surface;
    RasterClip clip;
    float basis[3][3]; /* model axis -> camera coordinates */
    float scale;
    float lightX;
    float lightY;
    float lightZ;
    float lightLength;
    int viewLeft;
    int viewTop;
    int viewWidth;
    int viewHeight;
    float halfWidth;
    float halfHeight;
    int minX;
    int minY;
    int maxX;
    int maxY;
    int index;
    float sun[3];
    float sunColour[3];
    float silhouette;
    float keyScale;
    int rockType;
    int modelType;

    if (viewport == NULL || viewport->pixels == NULL)
        return 0;
    rockType = GetRock3dModelType(obj);
    if (rockType >= 0) {
        modelType = rockType;
        model = (Ship3dModel *)GetShip3dModel(modelType);
        if (model == NULL ||
            !BuildRock3dShade(model, apObjShapeArray[obj]))
            return 0;
    } else {
        if (!g_bShip3dEnabled)
            return 0;
        if (aeObjectClass[obj] != OBJECT_CLASS_CAPITAL_SHIP &&
            aeObjectClass[obj] != OBJECT_CLASS_SHIP)
            return 0;
        if (g_aeShip3dPoseSource[obj] == SHIP3D_POSE_SPRITE_ONLY ||
            (g_aeShip3dPoseSource[obj] == SHIP3D_POSE_SPRITE_VIEW &&
             !ComputeShip3dSpriteBasis(obj, basis)))
            return 0;
        modelType = aeObjectTypeList[obj];
        model = (Ship3dModel *)GetShip3dModel(modelType);
        if (model == NULL || model->shade == NULL)
            return 0;
    }
    occlusion = GetShip3dOcclusion(model, modelType);
    scale = (float)(unsigned short)asObjScreenScalesArray[obj] / 256.0f;
    if (scale <= 0.0f)
        return 1;

    viewLeft = viewport->left;
    viewTop = viewport->top;
    viewWidth = viewport->right - viewport->left + 1;
    viewHeight = viewport->bottom - viewport->top + 1;
    if (viewWidth <= 0 || viewHeight <= 0 ||
        viewWidth * viewHeight > SHIP3D_MAX_VIEWPORT_PIXELS)
        return 1;
    if (g_nShip3dDepthPixels < viewWidth * viewHeight) {
        SDL_free(g_pfShip3dDepth);
        g_pfShip3dDepth = SDL_malloc(
            (size_t)viewWidth * viewHeight * sizeof(float));
        g_nShip3dDepthPixels = g_pfShip3dDepth != NULL ?
            viewWidth * viewHeight : 0;
        if (g_pfShip3dDepth == NULL)
            return 1;
    }

    if (rockType >= 0)
        ComputeRock3dBasis(obj, model, basis);
    else
        SdlShip3dComputeBasis(obj, basis);
    if (!SdlBackdropLightCamera(g_afShip3dRimLight)) {
        g_afShip3dRimLight[0] = 0.57735027f;
        g_afShip3dRimLight[1] = 0.57735027f;
        g_afShip3dRimLight[2] = 0.57735027f;
    }

    /* Unit vector from the surface towards the light, in camera space
     * (x right, y down, z forward): above, left of and behind the viewer.
     * The relighting fit saw n.v between 0.3 and 1 with the light at the
     * camera, and this light is at about 0.73 to the camera axis. */
    lightX = -0.40f;
    lightY = -0.55f;
    lightZ = -0.73f;
    lightLength = SDL_sqrtf(lightX * lightX + lightY * lightY +
                            lightZ * lightZ);
    lightX /= lightLength;
    lightY /= lightLength;
    lightZ /= lightLength;

    /* --glare: a ship in front of the sun shows its dark side. */
    silhouette = SdlGlareSilhouette(centerX, centerY,
                                    GetShip3dRadius(model) * scale, sun,
                                    sunColour);
    keyScale = 1.0f - SHIP3D_SILHOUETTE_KEY * silhouette;

    ValidateViewportBounds(viewport, &surface, &clip);

    /* Half the screen extent of one voxel cube under this orientation. */
    halfWidth = 0.5f * SHIP3D_FOOTPRINT * scale *
                (SDL_fabsf(basis[0][0]) + SDL_fabsf(basis[0][1]) +
                 SDL_fabsf(basis[0][2]));
    halfHeight = 0.5f * SHIP3D_FOOTPRINT * scale *
                 (SDL_fabsf(basis[1][0]) + SDL_fabsf(basis[1][1]) +
                  SDL_fabsf(basis[1][2]));

    if (SdlShip3dUsesGl()) {
        int recorded = 0;
        int pointCount = 0;
        int trueColour = g_bShip3dTrueColour && model->albedo != NULL;
        float coverExtent = halfWidth > halfHeight ? halfWidth : halfHeight;

        if (g_nShip3dGlPointCapacity < model->count) {
            SDL_free(g_pfShip3dGlPoints);
            g_pfShip3dGlPoints = SDL_malloc((size_t)model->count *
                                            SHIP3D_GL_POINT_FLOATS *
                                            sizeof(float));
            g_nShip3dGlPointCapacity = g_pfShip3dGlPoints != NULL ?
                model->count : 0;
        }
        if (g_pfShip3dGlPoints != NULL) {
            for (index = 0; index < model->count; index++) {
                const Ship3dPoint *point = &model->points[index];
                float cameraX = basis[0][0] * point->x +
                                basis[0][1] * point->y +
                                basis[0][2] * point->z;
                float cameraY = basis[1][0] * point->x +
                                basis[1][1] * point->y +
                                basis[1][2] * point->z;
                float cameraZ = basis[2][0] * point->x +
                                basis[2][1] * point->y +
                                basis[2][2] * point->z;
                float normalX = basis[0][0] * point->nx +
                                basis[0][1] * point->ny +
                                basis[0][2] * point->nz;
                float normalY = basis[1][0] * point->nx +
                                basis[1][1] * point->ny +
                                basis[1][2] * point->nz;
                float normalZ = basis[2][0] * point->nx +
                                basis[2][1] * point->ny +
                                basis[2][2] * point->nz;
                float *out;
                float depth;

                if (normalZ > 0.35f)
                    continue;
                out = g_pfShip3dGlPoints +
                      (size_t)pointCount * SHIP3D_GL_POINT_FLOATS;
                out[0] = centerX + cameraX * scale - 0.5f;
                out[1] = centerY + cameraY * scale - 0.5f;
                if (silhouette > 0.0f)
                    SdlGlareCoverPoint(out[0] + 0.5f, out[1] + 0.5f,
                                       coverExtent);
                depth = cameraZ * scale * (1.0f / 8192.0f);
                out[2] = depth < -0.99f ? -0.99f :
                         depth > 0.99f ? 0.99f : depth;
                {
                    float facing = normalX * lightX + normalY * lightY +
                                   normalZ * lightZ;
                    float sunRim = GetShip3dSunRim(silhouette, sun, normalX,
                                                   normalY, normalZ);

                    if (facing < 0.0f)
                        facing = 0.0f;
                    facing = facing * keyScale +
                             GetShip3dRim(normalX, normalY, normalZ) +
                             GetShip3dFresnel(normalZ);
                    if (!trueColour)
                        facing += sunRim;
                    if (facing > 1.0f)
                        facing = 1.0f;
                    if (trueColour) {
                        const float *albedo = model->albedo + (size_t)index * 3;
                        float lit = 1.0f + model->kappa *
                                               (facing - model->reference);
                        int channel;

                        if (lit < 0.05f || model->unlit[index])
                            lit = model->unlit[index] ? 1.0f : 0.05f;
                        /* Lined up with the sun, the hull goes dark and the
                         * sun's own colour rims its edges. */
                        lit *= 1.0f - SHIP3D_SILHOUETTE_DARKEN * silhouette;
                        for (channel = 0; channel < 3; channel++) {
                            float value = albedo[channel] * lit +
                                          sunColour[channel] * sunRim;

                            out[3 + channel] = value > 1.0f ? 1.0f : value;
                        }
                    } else {
                        GetShip3dBlend(model->shade +
                                           (size_t)index * model->steps,
                                       model->steps,
                                       facing * (float)(model->steps - 1),
                                       out + 3);
                    }
                    out[6] = occlusion != NULL ? occlusion[index] : 1.0f;
                    if (!trueColour)
                        out[6] *= 1.0f - SHIP3D_SILHOUETTE_DARKEN * 0.6f *
                                             silhouette;
                }
                pointCount++;
            }
            recorded = SdlGlRendererRecordShip(
                viewport, g_pfShip3dGlPoints, pointCount,
                halfWidth > halfHeight ? halfWidth : halfHeight, trueColour);
        }
        if (recorded)
            return 1;
    }

    /* Screen bounding box of the splats, to clear the depth buffer. */
    minX = viewWidth;
    minY = viewHeight;
    maxX = -1;
    maxY = -1;
    for (index = 0; index < model->count; index++) {
        const Ship3dPoint *point = &model->points[index];
        float cameraX = basis[0][0] * point->x + basis[0][1] * point->y +
                        basis[0][2] * point->z;
        float cameraY = basis[1][0] * point->x + basis[1][1] * point->y +
                        basis[1][2] * point->z;
        int firstX;
        int lastX;
        int firstY;
        int lastY;

        GetShip3dSpan(centerX + cameraX * scale - viewLeft, halfWidth,
                      &firstX, &lastX);
        GetShip3dSpan(centerY + cameraY * scale - viewTop, halfHeight,
                      &firstY, &lastY);
        if (firstX < minX)
            minX = firstX;
        if (firstY < minY)
            minY = firstY;
        if (lastX > maxX)
            maxX = lastX;
        if (lastY > maxY)
            maxY = lastY;
    }
    if (minX < 0)
        minX = 0;
    if (minY < 0)
        minY = 0;
    if (maxX >= viewWidth)
        maxX = viewWidth - 1;
    if (maxY >= viewHeight)
        maxY = viewHeight - 1;
    if (maxX < minX || maxY < minY)
        return 1;
    {
        int row;

        for (row = minY; row <= maxY; row++) {
            int column;
            float *depth = g_pfShip3dDepth + row * viewWidth;

            for (column = minX; column <= maxX; column++)
                depth[column] = 3.0e38f;
        }
    }

    for (index = 0; index < model->count; index++) {
        const Ship3dPoint *point = &model->points[index];
        float cameraX = basis[0][0] * point->x + basis[0][1] * point->y +
                        basis[0][2] * point->z;
        float cameraY = basis[1][0] * point->x + basis[1][1] * point->y +
                        basis[1][2] * point->z;
        float cameraZ = basis[2][0] * point->x + basis[2][1] * point->y +
                        basis[2][2] * point->z;
        float normalX = basis[0][0] * point->nx + basis[0][1] * point->ny +
                        basis[0][2] * point->nz;
        float normalY = basis[1][0] * point->nx + basis[1][1] * point->ny +
                        basis[1][2] * point->nz;
        float normalZ = basis[2][0] * point->nx + basis[2][1] * point->ny +
                        basis[2][2] * point->nz;
        float facing;
        unsigned char colour;
        int firstX;
        int lastX;
        int firstY;
        int lastY;
        int x;
        int y;

        /* Facing away from the viewer: let the front surface win. */
        if (normalZ > 0.35f)
            continue;
        facing = normalX * lightX + normalY * lightY + normalZ * lightZ;
        if (facing < 0.0f)
            facing = 0.0f;
        if (occlusion != NULL)
            facing *= occlusion[index];
        facing += GetShip3dRim(normalX, normalY, normalZ) +
                  GetShip3dFresnel(normalZ);
        if (facing > 1.0f)
            facing = 1.0f;
        colour = model->shade[(size_t)index * model->steps +
                              (int)(facing * (float)(model->steps - 1) +
                                    0.5f)];
        GetShip3dSpan(centerX + cameraX * scale - viewLeft, halfWidth,
                      &firstX, &lastX);
        GetShip3dSpan(centerY + cameraY * scale - viewTop, halfHeight,
                      &firstY, &lastY);
        for (y = firstY < minY ? minY : firstY; y <= lastY && y <= maxY;
             y++) {
            float *depth = g_pfShip3dDepth + y * viewWidth;

            for (x = firstX < minX ? minX : firstX;
                 x <= lastX && x <= maxX; x++) {
                if (cameraZ < depth[x]) {
                    depth[x] = cameraZ;
                    SetRasterClipPixel(&clip, x, y, colour);
                }
            }
        }
    }
    return 1;
}

void SdlShip3dShutdown(void)
{
    int type;

    for (type = 0; type < SHIP3D_TYPE_COUNT; type++) {
        SDL_free(g_aShip3dModels[type].points);
        SDL_free(g_aShip3dModels[type].shade);
        SDL_free(g_aShip3dModels[type].occlusion);
        SDL_free(g_aShip3dModels[type].albedo);
        SDL_free(g_aShip3dModels[type].unlit);
        SDL_free(g_aShip3dModels[type].rockAlbedo);
        SDL_zero(g_aShip3dModels[type]);
    }
    SDL_free(g_pfShip3dDepth);
    g_pfShip3dDepth = NULL;
    g_nShip3dDepthPixels = 0;
    SDL_free(g_pfShip3dGlPoints);
    g_pfShip3dGlPoints = NULL;
    g_nShip3dGlPointCapacity = 0;
}
