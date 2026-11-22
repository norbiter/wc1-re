#include "wc1.h"

#include "video_internal.h"
#include "slint/launcher_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const g_apszSdlLauncherJoystickModes[] = {
    "original", "4button-2axis", "4button-4axis", "custom"
};

static const char *const g_apszSdlLauncherMusic[] = {
    "default", "adlib", "mt32"
};

static const char *const g_apszSdlLauncherJoystickAxes[] = {
    "twin-stick-roll", "twin-stick-yaw", "hotas-yaw", "hotas-roll",
    "linear-throttle", "rudder-yaw", "rudder-roll"
};

static int SdlFindLauncherChoice(const char *name,
                                 const char *const *choices, size_t choiceCount)
{
    size_t choice;

    for (choice = 0; choice < choiceCount; choice++) {
        if (strcmp(name, choices[choice]) == 0)
            return choice;
    }
    return -1;
}

/* The game later changes into GAMEDAT, so keep the ROM folder absolute. */
static int SdlSetMt32RomArgument(const char *directory, SdlLauncherOptions *options)
{
    char current[PATH_MAX];

    if (directory[0] == '\0')
        return 0;
    if (directory[0] == '/' || directory[0] == '\\' ||
        (directory[0] != '\0' && directory[1] == ':'))
        return SDL_strlcpy(options->mt32Roms, directory, sizeof(options->mt32Roms)) <
            sizeof(options->mt32Roms);
    return GetCurrentDirectoryA(sizeof(current), current) &&
        SDL_snprintf(options->mt32Roms, sizeof(options->mt32Roms), "%s/%s", current,
                     directory) < (int)sizeof(options->mt32Roms);
}

/* Features with an on/off switch.  Each takes --NAME and --no-NAME (for an
 * inverted one, --no-NAME turns the feature on), the last one given wins,
 * and a feature named on neither form takes its group's default once the
 * launcher's choices are known. */
enum SdlFeatureGroup {
    SDL_FEATURE_GROUP_DEFAULT,  /* on unless --classic */
    SDL_FEATURE_GROUP_ENHANCED, /* on with --enhanced unless --classic */
    SDL_FEATURE_GROUP_MODELS,   /* as ENHANCED, where ship3d models are found */
    SDL_FEATURE_GROUP_EASIER,   /* on with --easier */
    SDL_FEATURE_GROUP_OTHER,    /* off unless asked for */
    SDL_FEATURE_GROUP_DEBUG     /* off unless asked for; --help-debug */
};

enum SdlFeatureState {
    SDL_FEATURE_UNSET,
    SDL_FEATURE_ON,
    SDL_FEATURE_OFF
};

typedef struct SdlFeatureOption {
    const char *name;
    const char *valueName; /* also takes --NAME=VALUE; NULL for none */
    int group;
    int inverted;
    void (*apply)(const char *value);
    const char *help;
} SdlFeatureOption;

static void SdlApplyShieldFlash(const char *value) { (void)value; SdlShieldFlashEnable(); }
static void SdlApplyGunRange(const char *value) { (void)value; SdlGunRangeEnable(); }
static void SdlApplySmooth(const char *value) { (void)value; SdlSmoothEnable(); }
static void SdlApplyDust(const char *value) { (void)value; SdlDustEnable(); }
static void SdlApplyAspectless(const char *value) { (void)value; SdlAspectlessEnable(); }
static void SdlApplyFineStars(const char *value) { (void)value; SdlFineStarsEnable(); }
static void SdlApplyFineBackdrop(const char *value) { (void)value; SdlFineBackdropEnable(); }
static void SdlApplyFineShots(const char *value) { (void)value; SdlFineShotsEnable(); }
static void SdlApplyGlare(const char *value) { (void)value; SdlGlareEnable(); }
static void SdlApplyHdr(const char *value) { SdlHdrEnable(value ? atoi(value) : 0); }
static void SdlApplyTtfText(const char *value) { (void)value; SdlTtfTextEnable(); }
static void SdlApplyShip3d(const char *value) { SdlShip3dEnable(value); }
static void SdlApplyRock3d(const char *value) { (void)value; SdlRock3dEnable(); }
static void SdlApplyTrueColour(const char *value) { (void)value; SdlShip3dEnableTrueColour(); }
static void SdlApplyBackdropLight(const char *value) { (void)value; SdlBackdropLightEnable(); }
static void SdlApplyNoFriendlyFire(const char *value) { (void)value; SdlNoFriendlyFireEnable(); }
static void SdlApplyLowMissile(const char *value) { (void)value; SdlLowMissileEnable(); }
static void SdlApplySturdyFriends(const char *value) { (void)value; SdlSturdyFriendsEnable(); }
static void SdlApplyFairAsteroids(const char *value) { (void)value; SdlFairAsteroidsEnable(); }
static void SdlApplyAiThrottle(const char *value) { SdlAiThrottleEnable(value ? atoi(value) : 0); }
static void SdlApplyMouseCurve(const char *value) { SdlMouseCurveEnable(value ? atoi(value) : 0); }
static void SdlApplyJoystickCurve(const char *value) { SdlJoystickCurveEnable(value ? atoi(value) : 0); }
static void SdlApplyHitSpheres(const char *value) { (void)value; SdlHitSphereEnable(); }
static void SdlApplyMissionMenu(const char *value) { (void)value; SdlMissionMenuEnable(); }
static void SdlApplyJoystickDebug(const char *value) { (void)value; SdlEnableJoystickDebug(); }

static const SdlFeatureOption g_astSdlFeatures[] = {
    {"gun-range", NULL, SDL_FEATURE_GROUP_DEFAULT, 0, SdlApplyGunRange,
     "Crosshair dark until the target is in gun range"},
    {"shield-flash", NULL, SDL_FEATURE_GROUP_DEFAULT, 0, SdlApplyShieldFlash,
     "Flash a hemisphere over a ship when its shields take a hit"},
    {"smooth", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplySmooth,
     "Space flight at 60 fps (left off by --enhanced unless the display "
     "runs at a multiple of 60 Hz)"},
    {"dust", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyDust,
     "Fine space dust that streaks with speed"},
    {"aspectless", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyAspectless,
     "Cockpit-less flight fills a fullscreen display of any shape"},
    {"fine-stars", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyFineStars,
     "Stars drawn as sharp points of light with glare"},
    {"fine-backdrop", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyFineBackdrop,
     "Planets, suns and nebulae redrawn at display resolution"},
    {"fine-shots", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyFineShots,
     "Gun shots redrawn at display resolution as bolts"},
    {"glare", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyGlare,
     "A large sun dazzles: a haze round it, and 3D ships before it in "
     "silhouette"},
    {"hdr", "PCT", SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyHdr,
     "On a macOS HDR display, suns, glare and gun shots glow above white; "
     "PCT scales the extra brightness (default 100)"},
    {"ttf-text", NULL, SDL_FEATURE_GROUP_ENHANCED, 0, SdlApplyTtfText,
     "Text in the Space Wing Leader TrueType font at display resolution"},
    {"ship3d", "PATH", SDL_FEATURE_GROUP_MODELS, 0, SdlApplyShip3d,
     "Ships drawn from the 3D models in PATH (default: ship3d)"},
    {"rock3d", NULL, SDL_FEATURE_GROUP_MODELS, 0, SdlApplyRock3d,
     "Asteroids drawn as tumbling 3D models"},
    {"ship3d-truecolour", NULL, SDL_FEATURE_GROUP_MODELS, 0, SdlApplyTrueColour,
     "3D ships shaded in true colour"},
    {"backdrop-light", NULL, SDL_FEATURE_GROUP_MODELS, 0, SdlApplyBackdropLight,
     "3D models lit from the backdrop's sun or lit planet"},
    {"friendly-fire", NULL, SDL_FEATURE_GROUP_EASIER, 1, SdlApplyNoFriendlyFire,
     "--no-friendly-fire: shots and blasts spare ships on their own side; "
     "--friendly-fire keeps the original"},
    {"low-missile", NULL, SDL_FEATURE_GROUP_EASIER, 0, SdlApplyLowMissile,
     "Kilrathi ships carry at most one missile of each type"},
    {"sturdy-friends", NULL, SDL_FEATURE_GROUP_EASIER, 0, SdlApplySturdyFriends,
     "Double shields on friendly transports and capital ships"},
    {"fair-asteroids", NULL, SDL_FEATURE_GROUP_EASIER, 0, SdlApplyFairAsteroids,
     "No asteroids spawned on an intercept course"},
    {"ai-throttle", "N", SDL_FEATURE_GROUP_EASIER, 0, SdlApplyAiThrottle,
     "Above N fighters in flight (default 6), each decides less often"},
    {"mouse-curve", "PCT", SDL_FEATURE_GROUP_OTHER, 0, SdlApplyMouseCurve,
     "Smooth linear-to-exponential mouse steering; PCT scales sensitivity "
     "(default 100)"},
    {"joystick-curve", "PCT", SDL_FEATURE_GROUP_OTHER, 0, SdlApplyJoystickCurve,
     "The same curve for gamepad and joystick axes"},
    {"hitspheres", NULL, SDL_FEATURE_GROUP_DEBUG, 0, SdlApplyHitSpheres,
     "Show each ship's collision volume"},
    {"mission-menu", NULL, SDL_FEATURE_GROUP_DEBUG, 0, SdlApplyMissionMenu,
     "Hold Alt while loading a game to pick any mission"},
    {"joystick-debug", NULL, SDL_FEATURE_GROUP_DEBUG, 0, SdlApplyJoystickDebug,
     "Log joystick input"},
};

#define SDL_FEATURE_COUNT SDL_arraysize(g_astSdlFeatures)

static unsigned char g_aeSdlFeatureState[SDL_FEATURE_COUNT];
static const char *g_apszSdlFeatureValue[SDL_FEATURE_COUNT];
static int g_bSdlClassic;
static int g_bSdlEasier;

static int SdlFindFeature(const char *name)
{
    size_t feature;

    for (feature = 0; feature < SDL_FEATURE_COUNT; feature++) {
        if (strcmp(g_astSdlFeatures[feature].name, name) == 0)
            return (int)feature;
    }
    return -1;
}

/* Records --NAME, --NAME=VALUE or --no-NAME.  Returns 1 when the argument
 * names a feature, 0 when it does not, and -1 when it is malformed. */
static int SdlParseFeatureArgument(const char *argument)
{
    const SdlFeatureOption *option;
    const char *name;
    const char *equals;
    size_t length;
    size_t feature;
    int negative;

    name = argument + 2;
    negative = strncmp(name, "no-", 3) == 0;
    if (negative)
        name += 3;
    equals = strchr(name, '=');
    length = equals != NULL ? (size_t)(equals - name) : strlen(name);
    for (feature = 0; feature < SDL_FEATURE_COUNT; feature++) {
        option = &g_astSdlFeatures[feature];
        if (strlen(option->name) == length &&
            strncmp(option->name, name, length) == 0)
            break;
    }
    if (feature == SDL_FEATURE_COUNT)
        return 0;
    if (equals != NULL && (negative || option->valueName == NULL ||
                           equals[1] == '\0')) {
        fprintf(stderr, "Option %s takes no value here.\n", argument);
        return -1;
    }
    g_aeSdlFeatureState[feature] =
        negative != option->inverted ? SDL_FEATURE_OFF : SDL_FEATURE_ON;
    if (equals != NULL)
        g_apszSdlFeatureValue[feature] = equals + 1;
    return 1;
}

/* Prints "  FLAG  TEXT" with TEXT wrapped into a column. */
static void SdlPrintUsageEntry(FILE *stream, const char *flag, const char *text)
{
    enum { COLUMN = 28, WIDTH = 79 };
    const char *word;
    size_t wordLength;
    int position;

    position = fprintf(stream, "  %s", flag);
    if (position >= COLUMN) {
        fputc('\n', stream);
        position = 0;
    }
    fprintf(stream, "%*s", COLUMN - position, "");
    position = COLUMN;
    while (*text != '\0') {
        word = text;
        wordLength = strcspn(word, " ");
        if (position > COLUMN && position + 1 + (int)wordLength > WIDTH) {
            fprintf(stream, "\n%*s", COLUMN, "");
            position = COLUMN;
        } else if (position > COLUMN) {
            fputc(' ', stream);
            position++;
        }
        fwrite(word, 1, wordLength, stream);
        position += (int)wordLength;
        text = word + wordLength;
        while (*text == ' ')
            text++;
    }
    fputc('\n', stream);
}

static void SdlPrintFeatureGroup(FILE *stream, int group, const char *heading)
{
    const SdlFeatureOption *option;
    char flag[64];
    size_t feature;
    int onByDefault;

    fprintf(stream, "\n%s\n", heading);
    onByDefault = group != SDL_FEATURE_GROUP_OTHER &&
                  group != SDL_FEATURE_GROUP_DEBUG;
    for (feature = 0; feature < SDL_FEATURE_COUNT; feature++) {
        option = &g_astSdlFeatures[feature];
        if (option->group != group)
            continue;
        SDL_snprintf(flag, sizeof(flag), "--%s%s%s%s%s",
                     onByDefault ? "[no-]" : option->inverted ? "no-" : "",
                     option->name, option->valueName ? "[=" : "",
                     option->valueName ? option->valueName : "",
                     option->valueName ? "]" : "");
        SdlPrintUsageEntry(stream, flag, option->help);
    }
}

static void SdlPrintUsage(FILE *stream)
{
    fputs("Usage: wc1 [options] [legacy game arguments]\n"
          "\n"
          "Options:\n"
          "  --help                  Show this list and exit\n"
          "  --help-debug            List the debugging options and exit\n"
          "  --gui                   Show the graphical launcher\n"
          "  --swc-demo DIR          Run the Super Wing Commander demo in DIR\n"
          "  --campaign=NAME         wc1, sm1 or sm2\n"
          "  --transfer-pilot        Transfer the pilot into the chosen campaign\n"
          "  --credits               Show the Super Wing Commander credits\n"
          "  --language=CODE         en, es, ja or fr\n"
          "  --voices=PATH           External voice catalog\n"
          "  --fullscreen, --windowed  Start fullscreen or in a window\n"
          "  --autosave, --no-autosave  Turn autosave checkpoints on or off\n"
          "  --enhanced              Draw at display resolution with OpenGL, and\n"
          "                          turn on the presentation features below\n"
          "  --easier                Turn on the difficulty features below\n"
          "  --classic               Leave every feature off unless named\n"
          "  --fov=DEGREES           Horizontal field of view of cockpit-less\n"
          "                          flight, 60-140 (default 90)\n"
          "  --ega                   EGA dithering\n"
          "  --music=DEVICE          default, adlib or mt32\n"
          "  --mt32-roms=PATH        MT-32 ROM folder\n"
          "  --mt32-model=MODEL      MT-32 model to emulate\n"
          "  --mt32-check            Describe the MT-32 ROMs found and exit\n"
          "  --joystick-rumble       Enable joystick rumble\n"
          "  --joystick-mode=MODE    original, 4button-2axis, 4button-4axis\n"
          "  --joystick-axes=MODE    twin-stick-roll, twin-stick-yaw, hotas-yaw,\n"
          "                          hotas-roll, linear-throttle, rudder-yaw,\n"
          "                          rudder-roll\n"
          "  --joystick-dead-zone=PCT  Dead zone for the extra joystick axes\n",
          stream);
    SdlPrintFeatureGroup(stream, SDL_FEATURE_GROUP_DEFAULT,
                         "On unless --classic:");
    SdlPrintFeatureGroup(stream, SDL_FEATURE_GROUP_ENHANCED,
                         "On with --enhanced:");
    SdlPrintFeatureGroup(stream, SDL_FEATURE_GROUP_MODELS,
                         "On with --enhanced when the ship3d models are found\n"
                         "(in ./ship3d, or ship3d beside or above the program):");
    SdlPrintFeatureGroup(stream, SDL_FEATURE_GROUP_EASIER,
                         "On with --easier:");
    SdlPrintFeatureGroup(stream, SDL_FEATURE_GROUP_OTHER, "Off unless named:");
    fputs("\n"
          "Other arguments (without a leading \"--\") are passed to the game as\n"
          "the original command-line tokens (b c f k q, Origin sN mN l, ...).\n",
          stream);
}

static void SdlPrintDebugUsage(FILE *stream)
{
    fputs("Debugging options:\n"
          "  --cheats                Enable cheat mode\n",
          stream);
    SdlPrintFeatureGroup(stream, SDL_FEATURE_GROUP_DEBUG, "Off unless named:");
}

/* The ship3d folder: the one --ship3d=PATH names, else the first holding
 * relit models of ./ship3d and ship3d beside or one level above the program,
 * which covers both an installed copy and a build in out-modern. */
static int SdlFindShip3dDirectory(const char *named, char *directory,
                                  size_t capacity, int rocks)
{
    static const char *const relative[] = {"ship3d", "../ship3d", "../../ship3d"};
    char *base;
    size_t candidate;

    if (named != NULL) {
        SDL_strlcpy(directory, named, capacity);
        return SdlShip3dHasModels(directory, rocks);
    }
    if (SdlShip3dHasModels("ship3d", rocks)) {
        SDL_strlcpy(directory, "ship3d", capacity);
        return 1;
    }
    base = SDL_GetBasePath();
    if (base == NULL) {
        SDL_ClearError();
        return 0;
    }
    for (candidate = 0; candidate < SDL_arraysize(relative); candidate++) {
        SDL_snprintf(directory, capacity, "%s%s", base, relative[candidate]);
        if (SdlShip3dHasModels(directory, rocks)) {
            SDL_free(base);
            return 1;
        }
    }
    SDL_free(base);
    return 0;
}

/* Gives each feature not named on the command line its group's default and
 * turns on the features that end up on.  --smooth waits for the window. */
static void SdlResolveFeatures(int enhanced)
{
    static char directory[1024];
    const SdlFeatureOption *option;
    size_t feature;
    int ship3d;
    int rock3d;
    int shipModels;
    int rockModels;
    int implied;

    /* Implied models need the folder found; named ones search for it too,
     * but keep ./ship3d as the place to report missing files from. */
    ship3d = SdlFindFeature("ship3d");
    rock3d = SdlFindFeature("rock3d");
    shipModels = 0;
    rockModels = 0;
    if (g_aeSdlFeatureState[ship3d] != SDL_FEATURE_OFF ||
        g_aeSdlFeatureState[rock3d] != SDL_FEATURE_OFF) {
        shipModels = SdlFindShip3dDirectory(g_apszSdlFeatureValue[ship3d],
                                            directory, sizeof(directory), 0);
        rockModels = SdlFindShip3dDirectory(g_apszSdlFeatureValue[ship3d],
                                            directory, sizeof(directory), 1);
        if (shipModels || rockModels) {
            SdlFindShip3dDirectory(g_apszSdlFeatureValue[ship3d], directory,
                                   sizeof(directory), !shipModels);
            g_apszSdlFeatureValue[ship3d] = directory;
            SdlShip3dSetDirectory(directory);
        }
    }
    if (!enhanced || g_bSdlClassic) {
        shipModels = 0;
        rockModels = 0;
    }
    for (feature = 0; feature < SDL_FEATURE_COUNT; feature++) {
        option = &g_astSdlFeatures[feature];
        if (g_aeSdlFeatureState[feature] != SDL_FEATURE_UNSET)
            continue;
        switch (option->group) {
        case SDL_FEATURE_GROUP_DEFAULT:
            implied = !g_bSdlClassic;
            break;
        case SDL_FEATURE_GROUP_ENHANCED:
            implied = enhanced && !g_bSdlClassic;
#ifndef __APPLE__
            if (option->apply == SdlApplyHdr)
                implied = 0;
#endif
            break;
        case SDL_FEATURE_GROUP_MODELS:
            if (option->apply == SdlApplyRock3d)
                implied = rockModels;
            else if (option->apply == SdlApplyBackdropLight)
                implied = shipModels || rockModels;
            else
                implied = shipModels;
            break;
        case SDL_FEATURE_GROUP_EASIER:
            implied = g_bSdlEasier;
            break;
        default:
            implied = 0;
            break;
        }
        g_aeSdlFeatureState[feature] = implied ? SDL_FEATURE_ON : SDL_FEATURE_OFF;
        /* Only --smooth asks the display, which is not open yet. */
        if (implied && option->apply == SdlApplySmooth)
            g_aeSdlFeatureState[feature] = SDL_FEATURE_UNSET;
    }
    for (feature = 0; feature < SDL_FEATURE_COUNT; feature++) {
        option = &g_astSdlFeatures[feature];
        if (g_aeSdlFeatureState[feature] == SDL_FEATURE_ON &&
            option->apply != SdlApplySmooth)
            option->apply(g_apszSdlFeatureValue[feature]);
    }
}

/* --smooth paces its presents by the clock and shows each on a vertical
 * blank, so it judders unless the display refreshes at a multiple of 60 Hz.
 * Named on the command line it is kept; implied, it waits for such a
 * display.  An unknown rate is given the benefit of the doubt. */
static void SdlResolveSmooth(SDL_Window *window)
{
    SDL_DisplayMode mode;
    int feature;
    int display;
    int remainder;

    feature = SdlFindFeature("smooth");
    if (g_aeSdlFeatureState[feature] == SDL_FEATURE_UNSET) {
        display = SDL_GetWindowDisplayIndex(window);
        if (display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 &&
            mode.refresh_rate > 0) {
            remainder = mode.refresh_rate % 60;
            if (remainder > 1 && remainder < 59) {
                fprintf(stderr, "Smooth flight is off: the display runs at "
                        "%d Hz (--smooth turns it on anyway).\n",
                        mode.refresh_rate);
                g_aeSdlFeatureState[feature] = SDL_FEATURE_OFF;
                return;
            }
        }
        SDL_ClearError();
        g_aeSdlFeatureState[feature] = SDL_FEATURE_ON;
    }
    if (g_aeSdlFeatureState[feature] == SDL_FEATURE_ON)
        SdlApplySmooth(NULL);
}

static int SdlParsePortArguments(int *argumentCount, char **arguments,
                                  int *useLauncherGui,
                                  SdlLauncherOptions *options)
{
    char *argument;
    int argumentIndex;
    int outputArgumentIndex;
    int featureResult;

    outputArgumentIndex = 1;
    memset(options, 0, sizeof(*options));
    options->joystickDeadZone = SDL_JOYSTICK_DEAD_ZONE_ORIGINAL;
#ifdef WC1_STATIC_GUI
    *useLauncherGui = *argumentCount == 1;
#else
    *useLauncherGui = 0;
#endif
    argumentIndex = 1;
    while (argumentIndex < *argumentCount) {
        argument = arguments[argumentIndex];
        if (strcmp(argument, "--gui") == 0) {
            *useLauncherGui = 1;
        } else if (strcmp(argument, "--transfer-pilot") == 0) {
            options->transferPilot = 1;
        } else if (strcmp(argument, "--credits") == 0) {
            options->swcCredits = 1;
        } else if (strncmp(argument, "--campaign=", 11) == 0) {
            if (strcmp(argument + 11, "wc1") == 0)
                options->campaign = 0;
            else if (strcmp(argument + 11, "sm1") == 0)
                options->campaign = 1;
            else if (strcmp(argument + 11, "sm2") == 0)
                options->campaign = 2;
            else {
                fprintf(stderr, "Unknown campaign: %s (use wc1, sm1 or sm2).\n", argument + 11);
                return 0;
            }
        } else if (strncmp(argument, "--voices=", 9) == 0) {
            if (!argument[9] || SDL_strlcpy(options->voiceCatalog, argument + 9,
                                           sizeof(options->voiceCatalog)) >= sizeof(options->voiceCatalog)) {
                fprintf(stderr, "Invalid voice catalog path.\n");
                return 0;
            }
        } else if (strcmp(argument, "--fullscreen") == 0 ||
                   strcmp(argument, "--windowed") == 0) {
            options->fullscreen = strcmp(argument, "--fullscreen") == 0;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_FULLSCREEN;
        } else if (strcmp(argument, "--autosave") == 0 ||
                   strcmp(argument, "--no-autosave") == 0) {
            options->autosave = strcmp(argument, "--autosave") == 0;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_AUTOSAVE;
        } else if (strncmp(argument, "--language=", 11) == 0) {
            if (strcmp(argument + 11, "en") == 0)
                options->language = 0;
            else if (strcmp(argument + 11, "es") == 0)
                options->language = 1;
            else if (strcmp(argument + 11, "ja") == 0)
                options->language = 2;
            else if (strcmp(argument + 11, "fr") == 0)
                options->language = 3;
            else {
                fprintf(stderr, "Unknown language: %s (use en, es, ja or fr).\n", argument + 11);
                return 0;
            }
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_LANGUAGE;
        } else if (strcmp(argument, "--help") == 0) {
            SdlPrintUsage(stdout);
            exit(0);
        } else if (strcmp(argument, "--check") == 0) {
            fprintf(stderr, "The --check runtime mode has been removed.\n");
            return 0;
        } else if (strcmp(argument, "--enhanced") == 0) {
            options->enhancedRenderer = 1;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_ENHANCED;
        } else if (strcmp(argument, "--help-debug") == 0) {
            SdlPrintDebugUsage(stdout);
            exit(0);
        } else if (strcmp(argument, "--easier") == 0) {
            g_bSdlEasier = 1;
        } else if (strcmp(argument, "--classic") == 0) {
            g_bSdlClassic = 1;
        } else if (strncmp(argument, "--fov=", 6) == 0) {
            SdlFovSet(atoi(argument + 6));
        } else if (strcmp(argument, "--cheats") == 0) {
            options->cheatMode = 1;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_CHEATS;
        } else if (strcmp(argument, "--joystick-rumble") == 0) {
            options->joystickRumble = 1;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_RUMBLE;
        } else if (strncmp(argument, "--joystick-mode=", 16) == 0) {
            options->joystickMode = SdlFindLauncherChoice(
                argument + 16, g_apszSdlLauncherJoystickModes,
                SDL_arraysize(g_apszSdlLauncherJoystickModes));
            if (options->joystickMode < 0) {
                fprintf(stderr, "Unknown joystick mode: %s\n",
                        argument + 16);
                return 0;
            }
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_JOYSTICK_MODE;
        } else if (strncmp(argument, "--joystick-axes=", 16) == 0) {
            options->joystickAxes = SdlFindLauncherChoice(
                argument + 16, g_apszSdlLauncherJoystickAxes,
                SDL_arraysize(g_apszSdlLauncherJoystickAxes));
            if (options->joystickAxes < 0) {
                fprintf(stderr, "Unknown joystick axes mode: %s\n",
                        argument + 16);
                return 0;
            }
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_JOYSTICK_AXES;
        } else if (strncmp(argument, "--joystick-dead-zone=", 21) == 0) {
            char *end;
            long percent = strtol(argument + 21, &end, 10);

            if (end == argument + 21 || *end != '\0' || percent < 0 ||
                percent > SDL_JOYSTICK_DEAD_ZONE_MAXIMUM) {
                fprintf(stderr, "Invalid joystick dead zone: %s (use 0 to %d percent; "
                        "the original is %d).\n", argument + 21,
                        SDL_JOYSTICK_DEAD_ZONE_MAXIMUM, SDL_JOYSTICK_DEAD_ZONE_ORIGINAL);
                return 0;
            }
            options->joystickDeadZone = (int)percent;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_JOYSTICK_DEAD_ZONE;
        } else if (strncmp(argument, "--music=", 8) == 0) {
            options->musicDevice = SdlFindLauncherChoice(
                argument + 8, g_apszSdlLauncherMusic,
                SDL_arraysize(g_apszSdlLauncherMusic));
            if (options->musicDevice < 0) {
                fprintf(stderr, "Unknown music: %s (use default, adlib or mt32)\n",
                        argument + 8);
                return 0;
            }
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_MUSIC;
        } else if (strncmp(argument, "--mt32-roms=", 12) == 0) {
            if (!SdlSetMt32RomArgument(argument + 12, options)) {
                fprintf(stderr, "Invalid MT-32 ROM folder: %s\n", argument + 12);
                return 0;
            }
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_MT32_ROMS;
            /* A ROM folder alone asks for the MT-32. */
            if (!(options->explicitOptions & SDL_LAUNCHER_EXPLICIT_MUSIC)) {
                options->musicDevice = SDL_LAUNCHER_MUSIC_MT32;
                options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_MUSIC;
            }
        } else if (strncmp(argument, "--mt32-model=", 13) == 0) {
            if (SDL_strlcpy(options->mt32Model, argument + 13, sizeof(options->mt32Model)) >=
                sizeof(options->mt32Model)) {
                fprintf(stderr, "Invalid MT-32 model: %s\n", argument + 13);
                return 0;
            }
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_MT32_MODEL;
        } else if (strcmp(argument, "--mt32-check") == 0) {
            char description[PATH_MAX + 256];
            int usable = SdlDescribeMt32Roms(options->mt32Roms, options->mt32Model,
                                             description, sizeof(description));

            printf("%s\n", description);
            exit(usable ? 0 : 1);
        } else if (strcmp(argument, "--ega") == 0) {
            options->egaDither = 1;
            options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_EGA;
        } else if (argument[0] == '-' && argument[1] == '-' &&
                   (featureResult = SdlParseFeatureArgument(argument)) != 0) {
            if (featureResult < 0)
                return 0;
        } else if (argument[0] == '-' && argument[1] == '-') {
            fprintf(stderr, "Unknown option: %s\n\n", argument);
            SdlPrintUsage(stderr);
            return 0;
        } else {
            if ((argument[0] == '-' ? argument[1] : argument[0]) == 'c') {
                options->cockpitlessView = 1;
                options->explicitOptions |= SDL_LAUNCHER_EXPLICIT_COCKPITLESS;
            }
            arguments[outputArgumentIndex++] = argument;
        }
        argumentIndex++;
    }
    *argumentCount = outputArgumentIndex;
    arguments[outputArgumentIndex] = 0;
    return 1;
}

/* WC1 data, DOS or Kilrathi Saga, always has GAMEDAT/MODULE.000. */
static int SdlHasWc1GameData(void)
{
    char path[PATH_MAX];
    SDL_RWops *file;
    Sint64 size;

    if (!SdlResolvePath("GAMEDAT/MODULE.000", path, sizeof(path)))
        return 0;
    file = SDL_RWFromFile(path, "rb");
    if (file == NULL) {
        SDL_ClearError();
        return 0;
    }
    size = SDL_RWsize(file);
    SDL_RWclose(file);
    return size > 0;
}

int SdlHasSwcResources(const char *directory)
{
    char path[PATH_MAX];
    int found = SdlFindSwcResources(directory, path, sizeof(path));

    SDL_ClearError();
    return found;
}

static int SdlOpenLauncherGui(SdlLauncherOptions *options)
{
#ifdef WC1_STATIC_GUI
    return SdlRunLauncherGui(options);
#else
    (void)options;
    fprintf(stderr,
            "The graphical launcher is not included in this build.\n"
            "Build it with 'make modern-gui'.\n");
    return SDL_LAUNCHER_UNAVAILABLE;
#endif
}

static int SdlApplyLauncherOptions(const SdlLauncherOptions *options)
{
    char directory[SDL_LAUNCHER_DIRECTORY_CAPACITY];
    if (options->campaign < 0 || options->campaign > 2 ||
        (options->swcDemo && options->campaign != 0) ||
        (options->transferPilot && !options->campaign)) {
        fprintf(stderr, "Invalid campaign for the selected game.\n");
        return 0;
    }
    if (options->swcCredits && !options->swcDemo) {
        fprintf(stderr, "Credits are available only for Super Wing Commander.\n");
        return 0;
    }
    nLaunchCampaign = options->campaign;
    nPilotTransferState = options->transferPilot != 0;
    if (options->joystickMode < SDL_LAUNCHER_JOYSTICK_ORIGINAL ||
        options->joystickMode > SDL_LAUNCHER_JOYSTICK_CUSTOM ||
        options->joystickAxes < SDL_LAUNCHER_AXES_TWIN_STICK_ROLL ||
        options->joystickAxes > SDL_LAUNCHER_AXES_RUDDER_ROLL) {
        fprintf(stderr, "Invalid launcher joystick options.\n");
        return 0;
    }
    SdlSetJoystickMode(g_apszSdlLauncherJoystickModes[options->joystickMode]);
    SdlSetJoystickAxesMode(g_apszSdlLauncherJoystickAxes[options->joystickAxes]);
    SdlSetPadBindings(options->padBindings);
    if (!SdlSetJoystickDeadZone(options->joystickDeadZone)) {
        fprintf(stderr, "Invalid joystick dead zone.\n");
        return 0;
    }
    SdlSetKeyBindings(options->keyBindings);
    SdlConfigureAutosave(options->autosave);
    if (options->musicDevice < SDL_LAUNCHER_MUSIC_DEFAULT ||
        options->musicDevice > SDL_LAUNCHER_MUSIC_MT32) {
        fprintf(stderr, "Invalid music selection.\n");
        return 0;
    }
    SdlSetMusicDevice(options->musicDevice);
    SdlSetMt32Roms(options->mt32Roms, options->mt32Model);
    if (*options->voiceCatalog && !options->swcDemo && !SdlLoadVoiceCatalog(options->voiceCatalog)) {
        fprintf(stderr, "Cannot load voices: %s\n", SDL_GetError());
        return 0;
    }
    if (!*options->voiceCatalog && !options->swcDemo) {
        if (!GetCurrentDirectoryA(sizeof(directory), directory) || !SdlLoadInstalledVoices(directory)) {
            fprintf(stderr, "Cannot load voices: %s\n", SDL_GetError());
            return 0;
        }
    }
    /* A player's German fan translation of the DOS WC.EXE brings its own UI
     * text; the selectable catalogs translate the English data. */
    if (!options->swcDemo && !SdlUsingTownsData() &&
        SdlResolvePath("WC.EXE", directory, sizeof(directory)) &&
        SdlLoadGermanText(directory) > 0)
        fprintf(stderr, "Using the German text in WC.EXE%s.\n",
                options->language == 1 || options->language == 3 ? " instead of the selected language" : "");
    if (!SdlSetTownsLanguage(options->language == 2) ||
        !SdlSetLanguage(options->language != 2 && (options->swcDemo || SdlUsingTownsData() ||
                                                   SdlUsingGermanText()) ? 0 : options->language)) {
        fprintf(stderr, "Cannot load game language: %s\n", SDL_GetError());
        return 0;
    }
    /* Apply these only after the dialog so checked command-line defaults can
     * be turned off before the video and input systems are initialized. */
    if (options->joystickRumble)
        SdlEnableJoystickRumble();
    if (options->egaDither && !options->swcDemo)
        SdlEnableEgaDither();
    if (options->cheatMode) {
        /* CheckLauncherAndConfig (0x401d10) applies this same combination
         * for Saga's Cheater registry setting. Both SDL data paths share it. */
        nOriginDevUnlock = 1;
        bPlayerVulnerable = 0;
        bPlayerCollisionResponse = 0;
    }
    return 1;
}

/* SdlCalculateOutputViewport presents a 4:3 picture, so the window opens at
 * that shape: three times the 320x240 frame, or twice where the desktop is
 * too short for that and a title bar. */
static void SdlGetInitialWindowSize(int *width, int *height)
{
    SDL_Rect usable;
    int scale;

    scale = 3;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 &&
        usable.h < 240 * scale + 64)
        scale = 2;
    *width = 320 * scale;
    *height = *width * SDL_PORT_DISPLAY_ASPECT_HEIGHT /
        SDL_PORT_DISPLAY_ASPECT_WIDTH;
}

static void SdlApplyLegacyArguments(int argumentCount, char **arguments)
{
    const char *argument;
    char command;
    int argumentIndex;

    argumentIndex = 1;
    while (argumentIndex < argumentCount) {
        argument = arguments[argumentIndex];
        if (strcmp(argument, "$#SAGA.EXE") == 0)
            bShowKilrathiSagaCredits = 1;
        command = argument[0] == '-' ? argument[1] : argument[0];
        switch (command) {
        case 'b':
            *(unsigned char *)&bPlayerCollisionResponse = 0;
            break;
        case 'c':
            bCockpitEnabled = 0;
            break;
        case 'f':
            bShowFrameRate = 1;
            break;
        case 'k':
            *(unsigned char *)&bPlayerVulnerable = 0;
            break;
        case 'q':
            bDirectDrawModeCascadeEnabled = 0;
            break;
        default:
            break;
        }
        argumentIndex++;
    }
}

int main(int argumentCount, char **arguments)
{
    SDL_Window *window;
    Uint32 windowFlags;
    int windowWidth;
    int windowHeight;
    int gameResult;
    int launcherResult;
    int useLauncherGui;
    int useEnhancedRenderer;
    int usingDosData;
    char swcPath[PATH_MAX];
    SdlLauncherOptions launcherOptions;

    if (!SdlParsePortArguments(&argumentCount, arguments,
                               &useLauncherGui, &launcherOptions))
        return 1;
    if (useLauncherGui) {
        launcherResult = SdlOpenLauncherGui(&launcherOptions);
        if (launcherResult == SDL_LAUNCHER_CANCELLED)
            return 0;
        if (launcherResult != SDL_LAUNCHER_ACCEPTED)
            return 1;
    } else {
        /* Like the launcher's check, the working directory selects the SWC
         * demo when it holds CMFs/Data.CMF and no WC1 game data. */
        launcherOptions.swcDemo = SdlFindSwcMissionData(swcPath, sizeof(swcPath)) &&
                                  !SdlHasWc1GameData();
    }
    if (!SdlApplyLauncherOptions(&launcherOptions))
        return 1;
    useEnhancedRenderer = launcherOptions.enhancedRenderer;
    SdlResolveFeatures(useEnhancedRenderer);
    if (useEnhancedRenderer)
        SdlSetVideoBackend(SDL_PORT_VIDEO_BACKEND_GL_SHARP_BILINEAR);
    if (launcherOptions.swcDemo) {
        if (!SdlFindSwcMissionData(swcPath, sizeof(swcPath))) {
            fprintf(stderr, "SWC needs CMFs/Data.CMF.\n");
            return 1;
        }
        gameResult = SdlRunSwcMission(swcPath, launcherOptions.cockpitlessView,
                                     launcherOptions.fullscreen, launcherOptions.swcCredits);
        if (gameResult != 0) {
            fprintf(stderr, "Cannot start SWC: %s\n", SDL_GetError());
            if (useLauncherGui)
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Wing Commander",
                                         SDL_GetError(), NULL);
        }
        return gameResult != 0;
    }
    /* The build defines SDL_MAIN_HANDLED, so tell SDL the process is already
     * up rather than letting it rename main() out from under us. */
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER |
                 SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    windowFlags = SDL_WINDOW_RESIZABLE;
    if (SdlCaptureHiddenWindow())
        windowFlags |= SDL_WINDOW_HIDDEN;
    if (launcherOptions.fullscreen)
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (!SdlConfigureVideoWindow(&windowFlags)) {
        fprintf(stderr, "SDL video configuration failed: %s\n",
                SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SdlGetInitialWindowSize(&windowWidth, &windowHeight);
    window =
        SDL_CreateWindow("Wing Commander SDL2 port", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight,
                         windowFlags);
    if (window == 0) {
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    /* Preserve Windows' no-IME game controls without disabling Japanese entry
     * process-wide. Other hosts change only for Japanese text fields. */
#ifndef _WIN32
    if (SdlUsingTownsJapanese())
#endif
    SDL_StopTextInput();
    SdlResolveSmooth(window);
    SdlCaptureInitialize();
    if (useLauncherGui && !SdlCaptureHiddenWindow()) {
        /* Hand focus from Slint's native event loop to the SDL game window. */
        SDL_ShowWindow(window);
        SDL_RaiseWindow(window);
        SDL_PumpEvents();
    }
    if (useEnhancedRenderer)
        fprintf(stderr,
                "Enhanced rendering enabled (GL sharp bilinear).\n");

    DIBinstall(window);
    hMainWindow = window;
    SdlStartEventPump();
    CheckLauncherAndConfig();
    usingDosData = SdlUsingDosData();
    if (usingDosData || SdlUsingTownsData()) {
        /* Only Saga supplies the ix audio streams. */
        bIxAudioEnabled = 0;
    }
    if (SdlUsingTownsData() && !SdlInitializeTownsAudio())
        fprintf(stderr, "FM Towns audio unavailable: %s\n", SDL_GetError());
    /* Kilrathi Saga data plays the intro with OriginFX in the enhanced
     * renderer, and all its music with OriginFX on an AdLib or MT-32. */
    if (usingDosData || (!SdlUsingTownsData() &&
                         (useEnhancedRenderer ||
                          launcherOptions.musicDevice != SDL_LAUNCHER_MUSIC_DEFAULT))) {
        if (!SdlInitializeOriginFxAudio(usingDosData)) {
            if (usingDosData) {
                fprintf(stderr,
                        "DOS audio is unavailable.\n");
            } else {
                fprintf(stderr, "%s is unavailable.\n",
                        launcherOptions.musicDevice != SDL_LAUNCHER_MUSIC_DEFAULT
                            ? "DOS music" : "OriginFX intro music");
            }
        }
    }
    SdlApplyLegacyArguments(argumentCount, arguments);
    if (useLauncherGui)
        bCockpitEnabled = !launcherOptions.cockpitlessView;
    MonoDebug_install();
    InitializeAudioSystem(window);
    InitializeAudioStreamer(window);
    srand(time(0));
    InitGameClockEpoch();
    CreateDebugOverlayConsole(0, window, 60, 20);
    nSessionStartTime = time(0);
    pCurrentEventManagerPump = 0;
    SDL_ShowCursor(SDL_DISABLE);
    gameResult = GameMain((argumentCount - 1), arguments);
    SdlSetMouseGrab(0);
    SDL_ShowCursor(SDL_ENABLE);
    DestroyGlobalDebugOverlayConsole();
    if ((dwIxStreamerStatus & 1) != 0)
        ix_streamer_destroy();
    ServiceAudioStream();
    SdlShutdownOriginFxAudio();
    SdlShutdownTownsAudio();
    SdlSetTownsLanguage(0);

    DIBunInstall();
    SdlShutdownJoysticks();
    SDL_DestroyWindow(window);
    SdlShutdownTranslations();
    SdlShutdownVoices();
    SDL_Quit();
    return gameResult;
}
