/* Headless capture for automated testing (fork-only).
 *
 * Driven entirely by environment variables, so a test harness can run the
 * game without a visible window and without touching the user's screen or
 * speakers:
 *
 *   WC1_CAPTURE_DIR=dir        PNG frames are written here.
 *   WC1_CAPTURE_INTERVAL=ms    Also save a frame every ms milliseconds.
 *   WC1_CAPTURE_AUDIO=file.wav The mixed game audio is recorded here.
 *   WC1_INPUT_SCRIPT=file      Timed key and mouse input, see below.
 *   WC1_HIDDEN=1               Never show the game window.
 *
 * Combine with SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy for a fully
 * private run (the classic renderer), or with WC1_HIDDEN=1 for --enhanced,
 * which needs a real OpenGL context.
 *
 * The input script holds one command per line; '#' starts a comment.
 * Commands run in order; only "wait" (and the hold time of "key"/"click")
 * advance the clock, which starts at the game's first event pump.
 *
 *   wait MS              let MS milliseconds pass
 *   key NAME [HOLD_MS]   press and release a key (SDL scancode name, e.g.
 *                        Return, Escape, Space, Up, A, F1); default hold 80
 *   down NAME / up NAME  press or release a key and leave it there
 *   text STRING          type each character of STRING as a key
 *   mouse X Y            move the pointer to 320x200 frame coordinates
 *   click X Y [right]    move there and click the left (or right) button
 *   shot [NAME]          save the next frame as NAME.png
 *   quit                 close the game
 */
#include "wc1.h"

#include "video_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAPTURE_EVENT_KEY 0
#define CAPTURE_EVENT_MOUSE 1
#define CAPTURE_EVENT_BUTTON 2
#define CAPTURE_EVENT_SHOT 3
#define CAPTURE_EVENT_QUIT 4

typedef struct CaptureEvent {
    Uint32 time;
    int kind;
    int pressed;
    SDL_Scancode key;
    int x;
    int y;
    Uint8 button;
    char name[64];
} CaptureEvent;

static int g_bCaptureInitialized;
static char g_szCaptureDir[1024];
static Uint32 g_dwCaptureInterval;
static Uint32 g_dwCaptureNextInterval;
static unsigned int g_nCaptureFrameNumber;
static SDL_atomic_t g_nCapturePending;
static char g_szCapturePendingName[64];

static CaptureEvent *g_pCaptureEvents;
static int g_nCaptureEventCount;
static int g_nCaptureEventNext;
static Uint32 g_dwCaptureScriptStart;
static Uint32 g_dwCaptureButtons;

static FILE *g_pCaptureAudioFile;
static Uint32 g_dwCaptureAudioBytes;
static SDL_mutex *g_pCaptureAudioLock;

/* ---------------------------------------------------------------- PNG */

static Uint32 g_adwCaptureCrcTable[256];

static void CaptureBuildCrcTable(void)
{
    Uint32 value;
    int index;
    int bit;

    for (index = 0; index < 256; index++) {
        value = (Uint32)index;
        for (bit = 0; bit < 8; bit++)
            value = (value & 1) ? 0xedb88320U ^ (value >> 1) : value >> 1;
        g_adwCaptureCrcTable[index] = value;
    }
}

static Uint32 CaptureCrc(Uint32 crc, const unsigned char *data, size_t size)
{
    size_t index;

    for (index = 0; index < size; index++)
        crc = g_adwCaptureCrcTable[(crc ^ data[index]) & 0xff] ^ (crc >> 8);
    return crc;
}

static void CapturePutBigEndian(unsigned char *out, Uint32 value)
{
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}

static void CaptureWriteChunk(FILE *file, const char *type,
                              const unsigned char *data, Uint32 size)
{
    unsigned char header[8];
    unsigned char trailer[4];
    Uint32 crc;

    CapturePutBigEndian(header, size);
    memcpy(header + 4, type, 4);
    crc = CaptureCrc(0xffffffffU, header + 4, 4);
    if (size != 0)
        crc = CaptureCrc(crc, data, size);
    CapturePutBigEndian(trailer, crc ^ 0xffffffffU);
    fwrite(header, 1, 8, file);
    if (size != 0)
        fwrite(data, 1, size, file);
    fwrite(trailer, 1, 4, file);
}

/* Writes RGB rows as a PNG with stored (uncompressed) deflate blocks: no
 * compression library is needed and the frames are small. */
static int CaptureWritePng(const char *path, const unsigned char *rgb,
                           int width, int height)
{
    static const unsigned char signature[8] = {
        0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    unsigned char ihdr[13];
    unsigned char *raw;
    unsigned char *zlib;
    size_t rawSize;
    size_t zlibSize;
    size_t offset;
    size_t out;
    Uint32 adlerA;
    Uint32 adlerB;
    FILE *file;
    int row;

    rawSize = (size_t)(width * 3 + 1) * height;
    raw = malloc(rawSize);
    zlib = malloc(rawSize + rawSize / 65535 * 5 + 16);
    if (raw == 0 || zlib == 0) {
        free(raw);
        free(zlib);
        return 0;
    }
    for (row = 0; row < height; row++) {
        raw[(size_t)row * (width * 3 + 1)] = 0;
        memcpy(raw + (size_t)row * (width * 3 + 1) + 1,
               rgb + (size_t)row * width * 3, (size_t)width * 3);
    }
    out = 0;
    zlib[out++] = 0x78;
    zlib[out++] = 0x01;
    offset = 0;
    while (offset < rawSize) {
        size_t block = rawSize - offset;

        if (block > 65535)
            block = 65535;
        zlib[out++] = offset + block == rawSize ? 1 : 0;
        zlib[out++] = (unsigned char)block;
        zlib[out++] = (unsigned char)(block >> 8);
        zlib[out++] = (unsigned char)~block;
        zlib[out++] = (unsigned char)(~block >> 8);
        memcpy(zlib + out, raw + offset, block);
        out += block;
        offset += block;
    }
    adlerA = 1;
    adlerB = 0;
    for (offset = 0; offset < rawSize; offset++) {
        adlerA = (adlerA + raw[offset]) % 65521;
        adlerB = (adlerB + adlerA) % 65521;
    }
    CapturePutBigEndian(zlib + out, (adlerB << 16) | adlerA);
    out += 4;
    zlibSize = out;

    file = fopen(path, "wb");
    if (file == 0) {
        free(raw);
        free(zlib);
        return 0;
    }
    CapturePutBigEndian(ihdr, (Uint32)width);
    CapturePutBigEndian(ihdr + 4, (Uint32)height);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* truecolour */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    fwrite(signature, 1, 8, file);
    CaptureWriteChunk(file, "IHDR", ihdr, 13);
    CaptureWriteChunk(file, "IDAT", zlib, (Uint32)zlibSize);
    CaptureWriteChunk(file, "IEND", 0, 0);
    fclose(file);
    free(raw);
    free(zlib);
    return 1;
}

/* ------------------------------------------------------------- Script */

static CaptureEvent *CaptureAddEvent(Uint32 time, int kind)
{
    CaptureEvent *events;
    CaptureEvent *event;

    events = realloc(g_pCaptureEvents,
                     sizeof(CaptureEvent) * (g_nCaptureEventCount + 1));
    if (events == 0)
        return 0;
    g_pCaptureEvents = events;
    event = &g_pCaptureEvents[g_nCaptureEventCount++];
    memset(event, 0, sizeof(*event));
    event->time = time;
    event->kind = kind;
    return event;
}

static int CaptureAddKey(Uint32 time, const char *name, int pressed,
                         const char *path, int lineNumber)
{
    CaptureEvent *event;
    SDL_Scancode key;

    key = SDL_GetScancodeFromName(name);
    if (key == SDL_SCANCODE_UNKNOWN) {
        fprintf(stderr, "%s:%d: unknown key '%s'\n", path, lineNumber, name);
        return 0;
    }
    event = CaptureAddEvent(time, CAPTURE_EVENT_KEY);
    if (event == 0)
        return 0;
    event->key = key;
    event->pressed = pressed;
    return 1;
}

static int CaptureAddCharacter(Uint32 time, char character)
{
    char name[2];

    if (character == ' ')
        return CaptureAddKey(time, "Space", 1, "", 0) &&
               CaptureAddKey(time + 60, "Space", 0, "", 0);
    name[0] = character;
    name[1] = 0;
    return CaptureAddKey(time, name, 1, "text", 0) &&
           CaptureAddKey(time + 60, name, 0, "text", 0);
}

static void CaptureLoadScript(const char *path)
{
    char line[512];
    char command[32];
    char argument[256];
    FILE *file;
    Uint32 time;
    int lineNumber;

    file = fopen(path, "r");
    if (file == 0) {
        fprintf(stderr, "Cannot open input script %s\n", path);
        return;
    }
    time = 0;
    lineNumber = 0;
    while (fgets(line, sizeof(line), file) != 0) {
        char *comment;
        char *rest;
        int fields;
        int x;
        int y;
        int hold;

        lineNumber++;
        comment = strchr(line, '#');
        if (comment != 0)
            *comment = 0;
        command[0] = 0;
        argument[0] = 0;
        if (sscanf(line, "%31s", command) != 1)
            continue;
        rest = strstr(line, command) + strlen(command);
        while (*rest == ' ' || *rest == '\t')
            rest++;
        rest[strcspn(rest, "\r\n")] = 0;
        if (strcmp(command, "wait") == 0) {
            time += (Uint32)strtoul(rest, 0, 10);
        } else if (strcmp(command, "key") == 0) {
            hold = 80;
            fields = sscanf(rest, "%255s %d", argument, &hold);
            if (fields < 1 || !CaptureAddKey(time, argument, 1, path,
                                             lineNumber))
                continue;
            time += (Uint32)hold;
            CaptureAddKey(time, argument, 0, path, lineNumber);
        } else if (strcmp(command, "down") == 0 ||
                   strcmp(command, "up") == 0) {
            CaptureAddKey(time, rest, command[0] == 'd', path, lineNumber);
        } else if (strcmp(command, "text") == 0) {
            for (; *rest != 0; rest++) {
                CaptureAddCharacter(time, *rest);
                time += 120;
            }
        } else if (strcmp(command, "mouse") == 0 ||
                   strcmp(command, "click") == 0) {
            CaptureEvent *event;

            if (sscanf(rest, "%d %d %255s", &x, &y, argument) < 2) {
                fprintf(stderr, "%s:%d: expected X Y\n", path, lineNumber);
                continue;
            }
            event = CaptureAddEvent(time, CAPTURE_EVENT_MOUSE);
            if (event == 0)
                continue;
            event->x = x;
            event->y = y;
            if (command[0] != 'c')
                continue;
            time += 50;
            event = CaptureAddEvent(time, CAPTURE_EVENT_BUTTON);
            if (event == 0)
                continue;
            event->x = x;
            event->y = y;
            event->pressed = 1;
            event->button = strcmp(argument, "right") == 0
                                ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            time += 80;
            event = CaptureAddEvent(time, CAPTURE_EVENT_BUTTON);
            if (event == 0)
                continue;
            event->x = x;
            event->y = y;
            event->button = strcmp(argument, "right") == 0
                                ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
        } else if (strcmp(command, "shot") == 0) {
            CaptureEvent *event = CaptureAddEvent(time, CAPTURE_EVENT_SHOT);

            if (event != 0)
                SDL_strlcpy(event->name, rest, sizeof(event->name));
        } else if (strcmp(command, "quit") == 0) {
            CaptureAddEvent(time, CAPTURE_EVENT_QUIT);
        } else {
            fprintf(stderr, "%s:%d: unknown command '%s'\n", path,
                    lineNumber, command);
        }
    }
    fclose(file);
    fprintf(stderr, "Input script %s: %d events over %u ms\n", path,
            g_nCaptureEventCount, (unsigned int)time);
}

/* --------------------------------------------------------------- Audio */

static void CaptureWriteWavSizes(void)
{
    unsigned char size[4];
    Uint32 value;
    long end;

    end = ftell(g_pCaptureAudioFile);
    value = g_dwCaptureAudioBytes + 36;
    size[0] = (unsigned char)value;
    size[1] = (unsigned char)(value >> 8);
    size[2] = (unsigned char)(value >> 16);
    size[3] = (unsigned char)(value >> 24);
    fseek(g_pCaptureAudioFile, 4, SEEK_SET);
    fwrite(size, 1, 4, g_pCaptureAudioFile);
    value = g_dwCaptureAudioBytes;
    size[0] = (unsigned char)value;
    size[1] = (unsigned char)(value >> 8);
    size[2] = (unsigned char)(value >> 16);
    size[3] = (unsigned char)(value >> 24);
    fseek(g_pCaptureAudioFile, 40, SEEK_SET);
    fwrite(size, 1, 4, g_pCaptureAudioFile);
    fseek(g_pCaptureAudioFile, end, SEEK_SET);
    fflush(g_pCaptureAudioFile);
}

static void CaptureOpenAudio(const char *path)
{
    /* 22050 Hz, 16-bit, stereo: SdlStartAudio's format. */
    static const unsigned char header[44] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
        0x22, 0x56, 0, 0, 0x88, 0x58, 0x01, 0, 4, 0, 16, 0,
        'd', 'a', 't', 'a', 0, 0, 0, 0};

    g_pCaptureAudioFile = fopen(path, "wb");
    if (g_pCaptureAudioFile == 0) {
        fprintf(stderr, "Cannot create audio capture %s\n", path);
        return;
    }
    fwrite(header, 1, sizeof(header), g_pCaptureAudioFile);
    fflush(g_pCaptureAudioFile);
    g_pCaptureAudioLock = SDL_CreateMutex();
}

void SdlCaptureAudio(const unsigned char *samples, unsigned int bytes)
{
    if (g_pCaptureAudioFile == 0 || g_pCaptureAudioLock == 0)
        return;
    SDL_LockMutex(g_pCaptureAudioLock);
    fwrite(samples, 1, bytes, g_pCaptureAudioFile);
    g_dwCaptureAudioBytes += bytes;
    /* Keep the header current so a killed run still leaves a valid file. */
    CaptureWriteWavSizes();
    SDL_UnlockMutex(g_pCaptureAudioLock);
}

/* ---------------------------------------------------------------- Setup */

void SdlCaptureInitialize(void)
{
    const char *value;

    if (g_bCaptureInitialized)
        return;
    g_bCaptureInitialized = 1;
    CaptureBuildCrcTable();
    value = SDL_getenv("WC1_CAPTURE_DIR");
    if (value != 0 && value[0] != 0)
        SDL_strlcpy(g_szCaptureDir, value, sizeof(g_szCaptureDir));
    value = SDL_getenv("WC1_CAPTURE_INTERVAL");
    if (value != 0)
        g_dwCaptureInterval = (Uint32)strtoul(value, 0, 10);
    value = SDL_getenv("WC1_CAPTURE_AUDIO");
    if (value != 0 && value[0] != 0)
        CaptureOpenAudio(value);
    value = SDL_getenv("WC1_INPUT_SCRIPT");
    if (value != 0 && value[0] != 0)
        CaptureLoadScript(value);
}

int SdlCaptureHiddenWindow(void)
{
    const char *value = SDL_getenv("WC1_HIDDEN");

    return value != 0 && value[0] != 0 && value[0] != '0';
}

/* ---------------------------------------------------------------- Frames */

int SdlCaptureFrameWanted(void)
{
    return g_szCaptureDir[0] != 0 && SDL_AtomicGet(&g_nCapturePending) != 0;
}

static void CaptureRequestFrame(const char *name)
{
    if (g_szCaptureDir[0] == 0)
        return;
    SDL_strlcpy(g_szCapturePendingName, name != 0 ? name : "",
                sizeof(g_szCapturePendingName));
    SDL_AtomicSet(&g_nCapturePending, 1);
}

void SdlCaptureFrameRgb(const unsigned char *rgb, int width, int height)
{
    char path[1200];

    if (!SdlCaptureFrameWanted())
        return;
    SDL_AtomicSet(&g_nCapturePending, 0);
    if (g_szCapturePendingName[0] != 0)
        SDL_snprintf(path, sizeof(path), "%s/%s.png", g_szCaptureDir,
                     g_szCapturePendingName);
    else
        SDL_snprintf(path, sizeof(path), "%s/frame-%05u-%07ums.png",
                     g_szCaptureDir, g_nCaptureFrameNumber,
                     (unsigned int)(SDL_GetTicks() - g_dwCaptureScriptStart));
    g_nCaptureFrameNumber++;
    if (CaptureWritePng(path, rgb, width, height))
        fprintf(stderr, "Captured %s\n", path);
    else
        fprintf(stderr, "Cannot write %s\n", path);
}

/* Takes the classic renderer's ARGB8888 frame. */
void SdlCaptureFrameArgb(const Uint32 *pixels, int width, int height)
{
    unsigned char *rgb;
    int index;

    if (!SdlCaptureFrameWanted())
        return;
    rgb = malloc((size_t)width * height * 3);
    if (rgb == 0)
        return;
    for (index = 0; index < width * height; index++) {
        rgb[index * 3] = (unsigned char)(pixels[index] >> 16);
        rgb[index * 3 + 1] = (unsigned char)(pixels[index] >> 8);
        rgb[index * 3 + 2] = (unsigned char)pixels[index];
    }
    SdlCaptureFrameRgb(rgb, width, height);
    free(rgb);
}

/* --------------------------------------------------------------- Pump */

static void CapturePushMouse(const CaptureEvent *script)
{
    SDL_Event event;
    int windowX;
    int windowY;

    if (!SdlMapLogicalToWindow(hMainWindow, script->x, script->y, &windowX,
                               &windowY))
        return;
    SDL_zero(event);
    if (script->kind == CAPTURE_EVENT_MOUSE) {
        event.type = SDL_MOUSEMOTION;
        event.motion.windowID = SDL_GetWindowID(hMainWindow);
        event.motion.x = windowX;
        event.motion.y = windowY;
        event.motion.state = g_dwCaptureButtons;
    } else {
        if (script->pressed)
            g_dwCaptureButtons |= SDL_BUTTON(script->button);
        else
            g_dwCaptureButtons &= ~(Uint32)SDL_BUTTON(script->button);
        event.type = script->pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.windowID = SDL_GetWindowID(hMainWindow);
        event.button.button = script->button;
        event.button.state = script->pressed ? SDL_PRESSED : SDL_RELEASED;
        event.button.clicks = 1;
        event.button.x = windowX;
        event.button.y = windowY;
    }
    SDL_PushEvent(&event);
}

static void CapturePushKey(const CaptureEvent *script)
{
    SDL_Event event;

    SDL_zero(event);
    event.type = script->pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.windowID = SDL_GetWindowID(hMainWindow);
    event.key.state = script->pressed ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = script->key;
    event.key.keysym.sym = SDL_GetKeyFromScancode(script->key);
    SDL_PushEvent(&event);
}

/* Called at the top of every event pump: queues due script input as SDL
 * events, so it takes exactly the path real input takes. */
void SdlCaptureServiceInput(void)
{
    Uint32 now;

    SdlCaptureInitialize();
    now = SDL_GetTicks();
    if (g_dwCaptureScriptStart == 0) {
        g_dwCaptureScriptStart = now;
        g_dwCaptureNextInterval = now;
    }
    if (g_dwCaptureInterval != 0 &&
        (Sint32)(now - g_dwCaptureNextInterval) >= 0) {
        g_dwCaptureNextInterval = now + g_dwCaptureInterval;
        if (!SdlCaptureFrameWanted())
            CaptureRequestFrame(0);
    }
    while (g_nCaptureEventNext < g_nCaptureEventCount &&
           now - g_dwCaptureScriptStart >=
               g_pCaptureEvents[g_nCaptureEventNext].time) {
        const CaptureEvent *script = &g_pCaptureEvents[g_nCaptureEventNext];
        SDL_Event quit;

        switch (script->kind) {
        case CAPTURE_EVENT_KEY:
            CapturePushKey(script);
            break;
        case CAPTURE_EVENT_MOUSE:
        case CAPTURE_EVENT_BUTTON:
            CapturePushMouse(script);
            break;
        case CAPTURE_EVENT_SHOT:
            /* A pending interval shot yields to the named one. */
            CaptureRequestFrame(script->name);
            break;
        case CAPTURE_EVENT_QUIT:
            SDL_zero(quit);
            quit.type = SDL_QUIT;
            SDL_PushEvent(&quit);
            break;
        default:
            break;
        }
        g_nCaptureEventNext++;
        /* One named shot per pump, so each gets its own frame. */
        if (script->kind == CAPTURE_EVENT_SHOT)
            break;
    }
}
