/* Fork-only: a signed distance field of each ship's hull, built from its
 * ship3d point cloud.  Negative inside the hull, positive outside, in model
 * units (the same units as the point cloud).
 *
 * The cloud is the surface of a voxel grid with a pitch of one model unit.
 * It is binned into a coarser grid, the exterior is flood-filled to tell
 * inside from outside, and an exact Euclidean distance transform gives the
 * distance to the nearest surface cell.  A sphere swept through the field can
 * then be stepped by (distance - radius), which cannot tunnel through thin
 * parts and needs no ray cast. */
#include "wc1.h"

#include "video_internal.h"

#define HULL_TYPE_COUNT 64
#define HULL_MAX_CELLS_ACROSS 128
#define HULL_PAD_CELLS 6
#define HULL_BIG 1.0e20f

struct HullField {
    int nx;
    int ny;
    int nz;
    float cell;
    float originX;
    float originY;
    float originZ;
    float radius;        /* bounding radius of the points */
    float *distance;
    int tried;
};

static HullField g_aHullFields[HULL_TYPE_COUNT];

/* Felzenszwalb & Huttenlocher squared distance transform of one line. */
static void TransformHullLine(const float *f, float *d, int n, int *v,
                              float *z)
{
    int k = 0;
    int q;

    v[0] = 0;
    z[0] = -HULL_BIG;
    z[1] = HULL_BIG;
    for (q = 1; q < n; q++) {
        float s;

        for (;;) {
            s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) /
                (2.0f * q - 2.0f * v[k]);
            if (s <= z[k] && k > 0)
                k--;
            else
                break;
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = HULL_BIG;
    }
    k = 0;
    for (q = 0; q < n; q++) {
        float delta;

        while (z[k + 1] < (float)q)
            k++;
        delta = (float)(q - v[k]);
        d[q] = delta * delta + f[v[k]];
    }
}

static void BuildHullField(HullField *field, int type)
{
    const float *xyz;
    int stride;
    int count;
    float minimum[3] = { 3.0e38f, 3.0e38f, 3.0e38f };
    float maximum[3] = { -3.0e38f, -3.0e38f, -3.0e38f };
    float extent;
    int dims[3];
    int total;
    int index;
    int axis;
    unsigned char *state; /* 1 surface, 2 exterior (flood-filled) */
    int *queue;
    float *work;
    float *line;
    float *lineOut;
    int *v;
    float *z;
    int longest;

    field->tried = 1;
    if (!SdlShip3dGetPoints(type, &xyz, &stride, &count) || count <= 0)
        return;
    for (index = 0; index < count; index++) {
        const float *point = xyz + (size_t)index * stride;

        for (axis = 0; axis < 3; axis++) {
            if (point[axis] < minimum[axis])
                minimum[axis] = point[axis];
            if (point[axis] > maximum[axis])
                maximum[axis] = point[axis];
        }
    }
    extent = 0.0f;
    for (axis = 0; axis < 3; axis++)
        if (maximum[axis] - minimum[axis] > extent)
            extent = maximum[axis] - minimum[axis];
    field->cell = extent / HULL_MAX_CELLS_ACROSS;
    if (field->cell < 1.0f)
        field->cell = 1.0f;
    for (axis = 0; axis < 3; axis++)
        dims[axis] = (int)SDL_ceilf((maximum[axis] - minimum[axis]) /
                                    field->cell) + 1 + 2 * HULL_PAD_CELLS;
    field->nx = dims[0];
    field->ny = dims[1];
    field->nz = dims[2];
    field->originX = minimum[0] - HULL_PAD_CELLS * field->cell;
    field->originY = minimum[1] - HULL_PAD_CELLS * field->cell;
    field->originZ = minimum[2] - HULL_PAD_CELLS * field->cell;
    total = dims[0] * dims[1] * dims[2];

    state = SDL_calloc((size_t)total, 1);
    queue = SDL_malloc((size_t)total * sizeof(int));
    field->distance = SDL_malloc((size_t)total * sizeof(float));
    longest = dims[0] > dims[1] ? dims[0] : dims[1];
    if (dims[2] > longest)
        longest = dims[2];
    line = SDL_malloc((size_t)longest * sizeof(float));
    lineOut = SDL_malloc((size_t)longest * sizeof(float));
    v = SDL_malloc((size_t)longest * sizeof(int));
    z = SDL_malloc((size_t)(longest + 1) * sizeof(float));
    work = field->distance;
    if (state == NULL || queue == NULL || work == NULL || line == NULL ||
        lineOut == NULL || v == NULL || z == NULL) {
        SDL_free(field->distance);
        field->distance = NULL;
        goto done;
    }

    field->radius = 0.0f;
    for (index = 0; index < count; index++) {
        const float *point = xyz + (size_t)index * stride;
        int cx = (int)((point[0] - field->originX) / field->cell);
        int cy = (int)((point[1] - field->originY) / field->cell);
        int cz = (int)((point[2] - field->originZ) / field->cell);
        float length = SDL_sqrtf(point[0] * point[0] + point[1] * point[1] +
                                 point[2] * point[2]);

        if (length > field->radius)
            field->radius = length;
        state[(cz * dims[1] + cy) * dims[0] + cx] = 1;
    }

    /* Flood the exterior from a padding corner.  Cells next to the surface
     * also block it, so a one-voxel gap in the shell cannot leak inwards. */
    {
        int head = 0;
        int tail = 0;
        static const int step[6][3] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
            { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
        };

        state[0] = 2;
        queue[tail++] = 0;
        while (head < tail) {
            int cell = queue[head++];
            int cx = cell % dims[0];
            int cy = (cell / dims[0]) % dims[1];
            int cz = cell / (dims[0] * dims[1]);
            int direction;

            for (direction = 0; direction < 6; direction++) {
                int nx = cx + step[direction][0];
                int ny = cy + step[direction][1];
                int nz = cz + step[direction][2];
                int neighbour;
                int near = 0;
                int other;

                if (nx < 0 || ny < 0 || nz < 0 || nx >= dims[0] ||
                    ny >= dims[1] || nz >= dims[2])
                    continue;
                neighbour = (nz * dims[1] + ny) * dims[0] + nx;
                if (state[neighbour] != 0)
                    continue;
                for (other = 0; other < 6 && !near; other++) {
                    int ox = nx + step[other][0];
                    int oy = ny + step[other][1];
                    int oz = nz + step[other][2];

                    if (ox >= 0 && oy >= 0 && oz >= 0 && ox < dims[0] &&
                        oy < dims[1] && oz < dims[2] &&
                        state[(oz * dims[1] + oy) * dims[0] + ox] == 1)
                        near = 1;
                }
                if (near)
                    continue;
                state[neighbour] = 2;
                queue[tail++] = neighbour;
            }
        }
    }

    /* Squared distance to the nearest surface cell, one axis at a time. */
    for (index = 0; index < total; index++)
        work[index] = state[index] == 1 ? 0.0f : HULL_BIG;
    for (axis = 0; axis < 3; axis++) {
        int strideAxis = axis == 0 ? 1 : axis == 1 ? dims[0]
                                                   : dims[0] * dims[1];
        int outer1 = axis == 0 ? dims[1] : dims[0];
        int outer2 = axis == 2 ? dims[1] : dims[2];
        int a;
        int b;

        for (a = 0; a < outer1; a++) {
            for (b = 0; b < outer2; b++) {
                int base;
                int i;

                if (axis == 0)
                    base = (b * dims[1] + a) * dims[0];
                else if (axis == 1)
                    base = b * dims[0] * dims[1] + a;
                else
                    base = b * dims[0] + a;
                for (i = 0; i < dims[axis]; i++)
                    line[i] = work[base + i * strideAxis];
                TransformHullLine(line, lineOut, dims[axis], v, z);
                for (i = 0; i < dims[axis]; i++)
                    work[base + i * strideAxis] = lineOut[i];
            }
        }
    }
    for (index = 0; index < total; index++) {
        float distance = SDL_sqrtf(work[index]) * field->cell;

        /* The surface lies half a voxel (0.5 model units) from the centres
         * of its voxels. */
        work[index] = state[index] == 2 ? distance - 0.5f
                                        : -distance - 0.5f;
    }
    fprintf(stderr, "hullfield: type %d, %dx%dx%d cells of %.2f, radius %.1f\n",
            type, dims[0], dims[1], dims[2], field->cell, field->radius);
done:
    SDL_free(state);
    SDL_free(queue);
    SDL_free(line);
    SDL_free(lineOut);
    SDL_free(v);
    SDL_free(z);
}

const HullField *SdlGetHullField(int type)
{
    HullField *field;

    if (type < 0 || type >= HULL_TYPE_COUNT)
        return NULL;
    field = &g_aHullFields[type];
    if (!field->tried)
        BuildHullField(field, type);
    return field->distance != NULL ? field : NULL;
}

float SdlGetHullFieldCell(const HullField *field)
{
    return field->cell;
}

float SdlGetHullFieldRadius(const HullField *field)
{
    return field->radius;
}

/* Trilinear sample in model units.  Outside the grid the value is the
 * nearest edge sample plus the distance to the grid, which never
 * overestimates the true distance. */
float SdlSampleHullField(const HullField *field, float x, float y, float z)
{
    float gx = (x - field->originX) / field->cell - 0.5f;
    float gy = (y - field->originY) / field->cell - 0.5f;
    float gz = (z - field->originZ) / field->cell - 0.5f;
    float outside = 0.0f;
    float fx;
    float fy;
    float fz;
    int ix;
    int iy;
    int iz;
    const float *d = field->distance;
    float c[2][2][2];
    int i;
    int j;
    int k;

    if (gx < 0.0f) {
        outside += gx * gx;
        gx = 0.0f;
    } else if (gx > field->nx - 1.001f) {
        outside += (gx - (field->nx - 1.001f)) * (gx - (field->nx - 1.001f));
        gx = field->nx - 1.001f;
    }
    if (gy < 0.0f) {
        outside += gy * gy;
        gy = 0.0f;
    } else if (gy > field->ny - 1.001f) {
        outside += (gy - (field->ny - 1.001f)) * (gy - (field->ny - 1.001f));
        gy = field->ny - 1.001f;
    }
    if (gz < 0.0f) {
        outside += gz * gz;
        gz = 0.0f;
    } else if (gz > field->nz - 1.001f) {
        outside += (gz - (field->nz - 1.001f)) * (gz - (field->nz - 1.001f));
        gz = field->nz - 1.001f;
    }
    ix = (int)gx;
    iy = (int)gy;
    iz = (int)gz;
    fx = gx - ix;
    fy = gy - iy;
    fz = gz - iz;
    for (k = 0; k < 2; k++)
        for (j = 0; j < 2; j++)
            for (i = 0; i < 2; i++)
                c[k][j][i] = d[((iz + k) * field->ny + iy + j) * field->nx +
                               ix + i];
    {
        float x00 = c[0][0][0] + (c[0][0][1] - c[0][0][0]) * fx;
        float x10 = c[0][1][0] + (c[0][1][1] - c[0][1][0]) * fx;
        float x01 = c[1][0][0] + (c[1][0][1] - c[1][0][0]) * fx;
        float x11 = c[1][1][0] + (c[1][1][1] - c[1][1][0]) * fx;
        float y0 = x00 + (x10 - x00) * fy;
        float y1 = x01 + (x11 - x01) * fy;

        return y0 + (y1 - y0) * fz +
               SDL_sqrtf(outside) * field->cell;
    }
}
