#pragma once
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
#include "Ntifs.h"
#include "Ntddmou.h"
#include "Ntddkbd.h"
#include "Kbdmou.h"




#define KBDCLASS_CONNECT_REQUEST 0x0B0203
#define MOUCLASS_CONNECT_REQUEST 0x0F0203
#define MOU_STRING_INC 0x14
#define KBD_STRING_INC 0x15




struct DEVOBJ_EXTENSION_FIX
{
    USHORT type;
    USHORT size;
    PDEVICE_OBJECT devObj;
    ULONGLONG PowerFlags;
    void *Dope;
    ULONGLONG ExtensionFlags;
    void *DeviceNode;
    PDEVICE_OBJECT AttachedTo;
};

/*
These routines are resolved after an adddevice call to mouclass and kbdclass.

MouClass and KbdClass then respond with a KBDCLASS_CONNECT_REQUEST or MOUCLASS_CONNECT_REQUEST as an internal
device request, giving us the linear address of the input functions. We then call them just like a real miniport driver
would ;p
*/
typedef void(__fastcall *MouseServiceDpc)(PDEVICE_OBJECT mou, PMOUSE_INPUT_DATA a1, PMOUSE_INPUT_DATA a2, PULONG a3);
typedef void(__fastcall *KeyboardServiceDpc)(PDEVICE_OBJECT kbd, PKEYBOARD_INPUT_DATA a1, PKEYBOARD_INPUT_DATA a2, PULONG a3);


typedef NTSTATUS(__fastcall *MouseAddDevice)(PDRIVER_OBJECT a1, PDEVICE_OBJECT a2);
typedef NTSTATUS(__fastcall *KeyboardAddDevice)(PDRIVER_OBJECT a1, PDEVICE_OBJECT a2);
typedef NTSTATUS(__fastcall *MmCopyVirtualMemory)(PEPROCESS a1, void *a2, PEPROCESS *a3, void *a4, ULONGLONG a5, KPROCESSOR_MODE a6, ULONG *a7);
typedef NTSTATUS(__fastcall *KbdclassRead)(PDEVICE_OBJECT device, PIRP irp);
typedef NTSTATUS(__fastcall *MouclassRead)(PDEVICE_OBJECT device, PIRP irp);
typedef NTSTATUS(__fastcall *kbdinput)(void *a1, void *a2, void *a3, void *a4, void *a5);
typedef NTSTATUS(__fastcall *mouinput)(void *a1, void *a2, void *a3, void *a4, void *a5);
typedef NTSTATUS(__fastcall *NtQuerySystemInformation)(ULONG infoclass, void *buffer, ULONG infolen, ULONG *plen);
typedef char*(_fastcall *PsGetProcessImageFileName)(PEPROCESS target);
typedef void*(_fastcall *PsGetProcessPeb)(PEPROCESS a1);
typedef void*(_fastcall *PsGetProcessWow64Process)(PEPROCESS a1);

extern NTSTATUS SystemRoutine();


extern PDEVICE_OBJECT mouTarget;
extern PDEVICE_OBJECT kbdTarget;
extern ULONG mouId;
extern ULONG kbdId;
extern PEPROCESS targetProcess;
extern PEPROCESS currentProcess;
extern char KEY_DATA[128];
extern char MOU_DATA[5];

extern MOUSE_INPUT_DATA mdata;
extern KEYBOARD_INPUT_DATA kdata;

extern KbdclassRead KbdClassReadRoutine;
extern MouclassRead MouClassReadRoutine;
extern MouseServiceDpc MouseDpcRoutine;
extern KeyboardServiceDpc KeyboardDpcRoutine;
extern MmCopyVirtualMemory MmCopyVirtualMemoryRoutine;
extern NtQuerySystemInformation ZwQuerySystemInformation;
extern kbdinput KeyboardInputRoutine;
extern mouinput MouseInputRoutine;
extern PsGetProcessImageFileName PsGetImageName;
extern PsGetProcessPeb PsGetPeb64;
extern PsGetProcessWow64Process PsGetPeb32;

extern PKEYBOARD_INPUT_DATA mjRead;
extern PMOUSE_INPUT_DATA mouIrp;


/*
This function calls the KeyboardClassServiceCallback routine given to us after we added our device,
via the internal device request. Before branching to the actual input routine, we must raise
our IRQL to dispatch level because we are pretending that it is a dpcforisr routine, not to mention
these routines acquire dispatch level spinlocks without checking or modifying the previous IRQL.
*/
void SynthesizeKeyboard(PKEYBOARD_INPUT_DATA a1);
/*
This function calls the MouseClassServiceCallback routine given to us after we added our device,
via the internal device request. Before branching to the actual input routine, we must raise
our IRQL to dispatch level because we are pretending that it is a dpcforisr routine, not to mention
these routines acquire dispatch level spinlocks without checking or modifying the previous IRQL.
*/
void SynthesizeMouse(PMOUSE_INPUT_DATA a1);

/*
This function asynchronously obtains the up/down keystate of the corresponding scan code.
This global array is updated each time the user presses a key, it is updated via the hijacked
InputApc queued to the CSRSS keyboard listener whenever the completion code calls IoCompleteRequest
*/
int GetKeyState(char scan);

/*
This function asynchronously obtains the up/down mouse button state of the corresponding mouse button.
This global array is updated each time the user presses a mouse button, it is updated via the hijacked
InputApc queued to the CSRSS mouse listener whenever the completion code calls IoCompleteRequest
*/
int GetMouseState(int key);

/*
This is just an easy to use wrapper around MmCopyVirtualMemory, as you can see a TargetProcess is required
or the call will just fail. Since MmCopyVirtualMemory contains it's own exception handling and data probes,
the call will fail if the source is bogus.
*/
NTSTATUS ReadMemory(void *source, void *target, ULONGLONG size);
NTSTATUS WriteMemory(void *source, void *target, ULONGLONG size);

/*
This is a wrapper around KeDelayExecutionThread. Since KeyDelayExecutionThread takes an input of 100ns units, we convert
our ms input, and use a negative value for relative time.
*/
NTSTATUS Sleep(ULONGLONG milliseconds);

//Image name is also in this structure, but we use msdn provided info because it is less likely to change.
struct SYSTEM_PROCESS_INFORMATION
{
    ULONG NextEntryOffset;
    char Reserved1[52];
    PVOID Reserved2[3];
    HANDLE UniqueProcessId;
    PVOID Reserved3;
    ULONG HandleCount;
    char Reserved4[4];
    PVOID Reserved5[11];
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER Reserved6[6];
};

//we could walk vads but since ldr_data's appearance on msdn it is less likely to change. 

/*
This is a very nasty function to retrieve the linear address of the specified module. It walks the PEB loader
lists of the target process and resolves the specified module name to a linear address.
*/
NTSTATUS GetModuleBase(wchar_t *ModuleName, ULONGLONG *base);

/*
This function uses ZwQuerySystemInformation and PsLookupProcessByProcessId to resolve the EPROCESS for the specified
image name.
*/
NTSTATUS AttachToProcess(char *ImageName);

/*
These are the hooks for win32k!InputApc which filter mouse and keyboard. ReadInstrumentation modifies the
completion KAPC by hooking mouclass and kbdclass MJ_READ routines. This is how HID/8042 input is monitored. Yes
this is nasty, but afaik only i8042 provides functions for filtering. The user could have 8042 or USB, or both.
*/
NTSTATUS KeyboardApc(void *a1, void *a2, void *a3, void *a4, void *a5);
NTSTATUS MouseApc(void *a1, void *a2, void *a3, void *a4, void *a5);

/*
These routines hijack the IRP's kapc for keyboard and mouse input respectively.
*/
NTSTATUS ReadInstrumentation(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS ReadInstrumentation1(PDEVICE_OBJECT device, PIRP irp);

/*
This routine serves to respond to the connect request from kbdclass/mouclass after an
adddevice call. The corresponding DPC routines are then saved in the global pointers described above.
*/
NTSTATUS Edox_InternalIoctl(PDEVICE_OBJECT device, PIRP irp);
NTSTATUS Edox_InvalidRequest(PDEVICE_OBJECT device, PIRP irp);

/*
This function recursively searches for a devnode to hijack in a device stack, this is so we can properly call
mouclass/kbdclass adddevice routines and pretend we are an actual HID provider. This is just a giant hack and our driver should
really be part of the USB stack.
*/
void *FindDevNodeRecurse(PDEVICE_OBJECT a1, ULONGLONG *a2);

/*==============================================================================*/

ULONG filter(void* a1);

NTSTATUS DriverEntry(IN PDRIVER_OBJECT driverObject, IN PUNICODE_STRING regPath);
NTSTATUS UnloadDriver(PDRIVER_OBJECT driverObject);
