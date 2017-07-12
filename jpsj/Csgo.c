/*
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "Input.h"

//////////////////////////////////
//                               //
//        FUNCTION LIST          //
//                               //
///////////////////////////////////                             

/*
Sleep(int milliseconds) - Just like usermode, takes this thread off the processor for the specified duration.

AttachToProcess(char *imageName) - Attaches to the specified process. The image name is just the binary, not a fully qualified image path.

GetModuleBase(wchar_t *moduleName, ULONGLONG *base) - obtains the linear base address for the specified module name. You must have previously and
successfully called AttachToProcess() or this function will fail.

ReadMemory(void *source, void *target, ULONGLONG size) - Speaks for itself I hope. You must have previously and successfully called AttachToProcess()
or this function will fail.

SynthesizeMouse(PMOUSE_INPUT_DATA a1) - Synthesizes the corresponding mouse input.

SynthesizeKeyboard(PMOUSE_INPUT_DATA a1) - Synthesizes the corresponding keyboard input.

GetKeyState(char scan) - Asynchronously retrieves the up or down state of the specified can code.

GetMouseState(int key) - Asynchronously retrieves the up or down state of the specified mouse button.

0 - Left mouse
1 - Right mouse
2 - Middle button
3 - Mouse button 4
4 - Mouse button 5
*/




#define LocalPlayer 0xAAEFFC
#define flags 0x100
#define forceJump 0x4F22638

#define EntityList 0x4A8B6A4
#define EntityListDistance 0x10

#define inCross 0xB2B4
#define iTeamNum 0xF0

typedef ULONGLONG QWORD;



//
//
//
QWORD GetLocalPlayer(QWORD clientDllBase) {

    QWORD localPlayer = NULL;

    QWORD ptr = (QWORD)((clientDllBase)+LocalPlayer);

    ReadMemory((void*)ptr, &localPlayer, 4);

    return localPlayer;
}

QWORD GetOnGround(QWORD localPlayer) {
    QWORD f = NULL;
    QWORD ptr = (QWORD)((localPlayer)+flags);
    ReadMemory((void*)ptr, &f, 4);
    return f;
}


//
//
//
int GetPlayers(QWORD *players, QWORD clientDllBase) {

    int x = 0;

    for (x = 0; x < 64; x++) {

        QWORD tempPlayerAddress = 0;

        QWORD ptr = (QWORD)(clientDllBase + EntityList + (x * EntityListDistance));

        if (!ReadMemory((void*)ptr, &tempPlayerAddress, 4)) {

            players[x] = tempPlayerAddress;

        }
        else {
            //ERROR: Cannot read Player
            return TRUE;
        }
    }
    return FALSE;
}

//
//
//
QWORD GetInCrossId(QWORD localPlayer) {

    QWORD ptr = (QWORD)((QWORD)localPlayer + inCross);

    QWORD inCrossId = NULL;

    if (!ReadMemory((void*)ptr, &inCrossId, 4))
    {

        return inCrossId;

    }
    else
    {
        //ERROR: Cannot read InCrossId from
        return NULL;
    }
}

//
//
//
int NotOnTeam(QWORD player, QWORD localPlayer) {

    QWORD playerTeamID = 0;
    QWORD localPlayerTeamID = 0;

    QWORD pPlayer = (QWORD)((QWORD)player + iTeamNum);
    QWORD pLocalPlayer = (QWORD)((QWORD)localPlayer + iTeamNum);

    if (!ReadMemory((void*)pPlayer, &playerTeamID, 4)) {

        if (!ReadMemory((void*)pLocalPlayer, &localPlayerTeamID, 4)) {

            if (playerTeamID == localPlayerTeamID)
                return FALSE;
            else
                return TRUE;

        }
        else {
            //ERROR: Cannot read team for localPlayer
            return FALSE;
        }
    }
    else {
        //ERROR: Cannot read team for player
        return TRUE;
    }
}


NTSTATUS SystemRoutine()
{

    //YOUR WORK HERE:


    ULONGLONG base;
    ULONG trigger;
    int active = 0;
    ULONGLONG page = 0;


    mdata.Flags |= MOUSE_MOVE_RELATIVE;

    //Use these scan codes for GetKeyState()
    //
    //http://msdn.microsoft.com/en-us/library/aa299374%28v=vs.60%29.aspx


    while (TRUE)
    {



#define __P 25//reacquire
#define  __V 47

        if (GetMouseState(3))
        {
            AttachToProcess("csgo.exe");
            GetModuleBase(L"client.dll", &base);
            active = 1;
        }

        if (active)
        {
            QWORD clientBase = (QWORD)base;
            QWORD localPlayer = GetLocalPlayer((QWORD)clientBase);

            QWORD onGroundFlags = GetOnGround((QWORD)localPlayer);

            if (GetKeyState(0x39) && onGroundFlags & 1)
            {
                ULONG flag = (ULONG)(0x5);
                WriteMemory((void*)flag, (void*)(clientBase + forceJump), 8);
                Sleep(50);
                flag = (ULONG)(0x4);
                WriteMemory((void*)flag, (void*)(clientBase + forceJump), 8);
            }

            //if activated
            /*
            //===============================================================
            //
            QWORD localPlayer = 0;

            QWORD players[64] = { 0 };

            QWORD clientBase = (QWORD)base;

            QWORD inCrossId = 0;

            //===============================================================
            //

            //Get local player address
            localPlayer = GetLocalPlayer((QWORD)clientBase);

            //Get player array
            GetPlayers(&players, (QWORD)clientBase);

            //Get id of player that is in cross, if any
            inCrossId = GetInCrossId(localPlayer);

            //===============================================================
            //

            //Check for valid player index
            if (inCrossId >= 1 && inCrossId <= 64) {

                //Check to see if they are on the same team
                if (NotOnTeam(players[inCrossId - 1], localPlayer)) {

                    mdata.ButtonFlags |= MOUSE_LEFT_BUTTON_DOWN;

                    //send the input
                    SynthesizeMouse(&mdata);

                    //lets wait 1/10 seconds and send the release

                    Sleep(50);

                    //remove button down flag
                    mdata.ButtonFlags &= ~MOUSE_LEFT_BUTTON_DOWN;

                    //send the button up
                    mdata.ButtonFlags |= MOUSE_LEFT_BUTTON_UP;

                    SynthesizeMouse(&mdata);

                }
            }
            */
        }

        //we should sleep here for a bit so this thread isn't using a lot of cpu time. same as user-mode.

        Sleep(1000);


    }


    return STATUS_SUCCESS;
}
