/* Fork-only: hold autopilot while a friendly ship is still due to jump in at
 * the current nav point.
 *
 * A warp-arrive ship waits as an invisible futurion until it jumps in, and
 * auto_pilot_sequence only takes along team members that are already ships.
 * In mission 7-1, clearing the Jalthi at Nav 2 before the second Drayman
 * arrived let the player autopilot home without it; the Drayman jumped in
 * later, far from the Claw, and the debriefing scored it as lost. */
#include <stdio.h>
#include "wc1.h"

const char *SdlAutopilotWaitReason(void)
{
    static char reason[40];
    short obj;
    short missionShip;
    const MissionShipRecord *record;

    for (obj = 1; obj < 10; obj++) {
        if (aeObjectClass[obj] != OBJECT_CLASS_FUTURION)
            continue;
        missionShip = nShipMissionIndices[obj];
        if (missionShip < 0 || missionShip >= ACTIVE_MISSION_SHIP_COUNT)
            continue;
        record = &aMissionShips[missionShip];
        if (record->side != SIDE_IMPERIAL ||
            record->missionType != MISSION_TYPE_WARP_ARRIVE ||
            record->navPoint != nCurrentNavigationPoint ||
            is_team_member(missionShip) == 0)
            continue;
        snprintf(reason, sizeof(reason), "%s Inbound",
                 aObjectTypeData[record->type].displayName);
        return reason;
    }
    return 0;
}
