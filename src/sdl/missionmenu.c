/* Fork-only: --mission-menu.  Holding Alt while a saved game loads opens a
 * debug picker that moves the loaded campaign to any series and mission.
 *
 * Series and mission counts come from the campaign data itself
 * (pMissionCampaignInfo, 0x5a bytes per series: +2 mission count, +6/+7 next
 * series and ship on success).  The picker is a single modal line driven by
 * the arrow keys: left/right change series, up/down change mission, Enter
 * accepts and Esc keeps the save as it was. */
#include "wc1.h"

#include <SDL.h>

#define MISSION_MENU_LAST_SERIES 13
#define MISSION_MENU_KEY_ESC 0x01
#define MISSION_MENU_KEY_ENTER 0x1c
#define MISSION_MENU_KEY_UP 0x48
#define MISSION_MENU_KEY_LEFT 0x4b
#define MISSION_MENU_KEY_RIGHT 0x4d
#define MISSION_MENU_KEY_DOWN 0x50

static int g_bMissionMenuEnabled;

void SdlMissionMenuEnable(void)
{
    g_bMissionMenuEnabled = 1;
}

static int MissionMenuMissionCount(int series)
{
    return (signed char)pMissionCampaignInfo[(series - 1) * 0x5a + 2];
}

static int MissionMenuSeriesValid(int series)
{
    int count;

    if (series < 1 || series > MISSION_MENU_LAST_SERIES)
        return 0;
    count = MissionMenuMissionCount(series);
    return count > 0 && count <= 8;
}

/* The ship flown is chosen by the series that leads into this one. */
static int MissionMenuShipFor(int series, int fallback)
{
    int from;

    for (from = 1; from <= MISSION_MENU_LAST_SERIES; from++) {
        if (MissionMenuSeriesValid(from) &&
            (signed char)pMissionCampaignInfo[(from - 1) * 0x5a + 6] ==
                series)
            return (signed char)pMissionCampaignInfo[(from - 1) * 0x5a + 7];
    }
    return fallback;
}

int SdlMissionMenuRequested(void)
{
    return g_bMissionMenuEnabled && (SDL_GetModState() & KMOD_ALT) != 0;
}

void SdlMissionMenuRun(struct CampaignState *campaign)
{
    int series;
    int mission;
    int step;
    int key;

    if (!SdlMissionMenuRequested())
        return;

    series = campaign->currentSeries;
    mission = campaign->currentMission;
    if (!MissionMenuSeriesValid(series))
        series = 1;
    if (mission >= MissionMenuMissionCount(series))
        mission = 0;

    for (;;) {
        if (ShowModalTextPanel(1, "Series %d Mission %d/%d  Enter=go Esc=no",
                               series, mission + 1,
                               MissionMenuMissionCount(series)) == 0)
            return;
        key = WaitForInputKey();
        ReleaseModalTextPanel();
        switch (key) {
        case MISSION_MENU_KEY_ESC:
            return;
        case MISSION_MENU_KEY_ENTER:
            campaign->playerShipType = (enum ObjectType)MissionMenuShipFor(
                series, campaign->playerShipType);
            campaign->currentSeries = (signed char)series;
            campaign->currentMission = (signed char)mission;
            campaign->seriesScore = 0;
            return;
        case MISSION_MENU_KEY_LEFT:
        case MISSION_MENU_KEY_RIGHT:
            step = key == MISSION_MENU_KEY_RIGHT ? 1 : -1;
            do {
                series += step;
                if (series < 1)
                    series = MISSION_MENU_LAST_SERIES;
                if (series > MISSION_MENU_LAST_SERIES)
                    series = 1;
            } while (!MissionMenuSeriesValid(series));
            mission = 0;
            break;
        case MISSION_MENU_KEY_UP:
        case MISSION_MENU_KEY_DOWN:
            mission += key == MISSION_MENU_KEY_DOWN ? 1 : -1;
            if (mission < 0)
                mission = MissionMenuMissionCount(series) - 1;
            if (mission >= MissionMenuMissionCount(series))
                mission = 0;
            break;
        }
    }
}
