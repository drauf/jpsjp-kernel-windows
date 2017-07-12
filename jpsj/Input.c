#include "Input.h"

PDEVICE_OBJECT mouTarget;
PDEVICE_OBJECT kbdTarget;
ULONG mouId = 0;
ULONG kbdId = 0;
PEPROCESS targetProcess = NULL;
PEPROCESS currentProcess = NULL;
char KEY_DATA[128];
char MOU_DATA[5];

MOUSE_INPUT_DATA mdata;
KEYBOARD_INPUT_DATA kdata;

KbdclassRead KbdClassReadRoutine;
MouclassRead MouClassReadRoutine;
MouseServiceDpc MouseDpcRoutine;
KeyboardServiceDpc KeyboardDpcRoutine;
MmCopyVirtualMemory MmCopyVirtualMemoryRoutine;
NtQuerySystemInformation ZwQuerySystemInformation;
kbdinput KeyboardInputRoutine = NULL;
mouinput MouseInputRoutine = NULL;
PsGetProcessImageFileName PsGetImageName;
PsGetProcessPeb PsGetPeb64;
PsGetProcessWow64Process PsGetPeb32;

PKEYBOARD_INPUT_DATA mjRead = NULL;
PMOUSE_INPUT_DATA mouIrp = NULL;

void SynthesizeKeyboard(PKEYBOARD_INPUT_DATA a1)
{
    KIRQL irql;
    char *endptr;
    ULONG fill = 1;


    endptr = (char*)a1;

    endptr += sizeof(KEYBOARD_INPUT_DATA);

    a1->UnitId = kbdId;

    //huehuehue
    KeRaiseIrql(DISPATCH_LEVEL, &irql);

    KeyboardDpcRoutine(kbdTarget, a1, (PKEYBOARD_INPUT_DATA)endptr, &fill);

    KeLowerIrql(irql);

}

void SynthesizeMouse(PMOUSE_INPUT_DATA a1)
{
    KIRQL irql;
    char *endptr;
    ULONG fill = 1;

    endptr = (char*)a1;

    endptr += sizeof(MOUSE_INPUT_DATA);

    a1->UnitId = mouId;

    //huehuehue
    KeRaiseIrql(DISPATCH_LEVEL, &irql);

    MouseDpcRoutine(mouTarget, a1, (PMOUSE_INPUT_DATA)endptr, &fill);

    KeLowerIrql(irql);

}

int GetKeyState(char scan)
{
    if (KEY_DATA[scan - 1]) return 1;

    return 0;
}

int GetMouseState(int key)
{
    if (MOU_DATA[key]) return 1;

    return 0;
}

NTSTATUS ReadMemory(void *source, void *target, ULONGLONG size)
{
    ULONG transferred;

    if (!targetProcess) return STATUS_INVALID_PARAMETER_1;

    return MmCopyVirtualMemoryRoutine(targetProcess, source, &currentProcess, target, size, KernelMode, &transferred);

}

NTSTATUS WriteMemory(void *source, void *target, ULONGLONG size)
{
    ULONG transferred;

    if (!targetProcess) return STATUS_INVALID_PARAMETER_1;

    return MmCopyVirtualMemoryRoutine(currentProcess, target, &targetProcess, source, size, KernelMode, &transferred);

}

NTSTATUS Sleep(ULONGLONG milliseconds)
{
    LARGE_INTEGER delay;
    ULONG *split;

    milliseconds *= 1000000;

    milliseconds /= 100;

    milliseconds = -milliseconds;

    split = (ULONG*)&milliseconds;

    delay.LowPart = *split;

    split++;

    delay.HighPart = *split;

    KeDelayExecutionThread(KernelMode, 0, &delay);

    return STATUS_SUCCESS;
}

NTSTATUS GetModuleBase(wchar_t *ModuleName, ULONGLONG *base)
{
    ULONGLONG ldr;
    ULONGLONG pdata = 0;
    ULONGLONG buffer = 0;
    ULONGLONG head = 0;
    ULONGLONG string = 0;
    wchar_t dllname[16];
    int i = 0;

    *base = 0;

    if (!targetProcess) return STATUS_INVALID_PARAMETER_1;

    ldr = (ULONGLONG)PsGetPeb32(targetProcess);

    if (!ldr)
    {
        ldr = (ULONGLONG)PsGetPeb64(targetProcess);

        ldr += 0x18;

        /**********************************************************/


        if (ReadMemory((void*)ldr, &pdata, 8)) return STATUS_INVALID_PARAMETER_1;

        pdata += 0x10;

        head = pdata;

        while (i < 500)
        {
            if (ReadMemory((void*)pdata, &buffer, 8)) return STATUS_INVALID_PARAMETER_1;

            if (buffer == head) return 1;

            buffer += 0x60;

            if (ReadMemory((void*)buffer, &string, 8)) return STATUS_INVALID_PARAMETER_1;

            if (ReadMemory((void*)string, dllname, sizeof(dllname))) return STATUS_INVALID_PARAMETER_1;

            if (!wcscmp(ModuleName, dllname))
            {
                buffer -= 0x30;

                if (ReadMemory((void*)buffer, &pdata, 8)) return STATUS_INVALID_PARAMETER_1;

                *base = pdata;

                return STATUS_SUCCESS;
            }

            i++;

            buffer -= 0x60;

            pdata = buffer;


        }



        return STATUS_INVALID_PARAMETER_1;

        /**********************************************************/

    }

    ldr += 0xc;

    if (ReadMemory((void*)ldr, &pdata, 4)) return STATUS_INVALID_PARAMETER_1;

    pdata += 0xc;

    head = pdata;

    while (i < 500)
    {
        if (ReadMemory((void*)pdata, &buffer, 4)) return STATUS_INVALID_PARAMETER_1;

        if (buffer == head) return 1;

        buffer += 0x30;

        if (ReadMemory((void*)buffer, &string, 4)) return STATUS_INVALID_PARAMETER_1;

        if (ReadMemory((void*)string, dllname, sizeof(dllname))) return STATUS_INVALID_PARAMETER_1;

        if (!wcscmp(ModuleName, dllname))
        {
            buffer -= 0x18;

            if (ReadMemory((void*)buffer, &pdata, 4)) return STATUS_INVALID_PARAMETER_1;

            *base = pdata;

            return STATUS_SUCCESS;
        }

        i++;

        buffer -= 0x30;

        pdata = buffer;


    }


    return STATUS_INVALID_PARAMETER_1;
}

NTSTATUS AttachToProcess(char *ImageName)
{

    ULONG entryOffset;
    NTSTATUS status;
    ULONG lenActual;
    struct SYSTEM_PROCESS_INFORMATION *sysinfo;
    char *sys;
    char *zero;
    PEPROCESS pbuffer;
    ANSI_STRING filename;
    char found = 0;

    currentProcess = PsGetCurrentProcess();

    if (targetProcess)
    {
        ObDereferenceObject(targetProcess);

        targetProcess = 0;
    }


    sys = (char*)ExAllocatePoolWithTag(PagedPool, 0x50000, NULL);

    zero = sys;

    sysinfo = (struct SYSTEM_PROCESS_INFORMATION*)sys;

    status = ZwQuerySystemInformation(0x5, (void*)sys, 0x50000, &lenActual);

    if (status)
    {
        ExFreePool((PVOID)sys);

        return STATUS_INVALID_PARAMETER_1;
    }

    while (1) //lul
    {

        if (!PsLookupProcessByProcessId(sysinfo->UniqueProcessId, &pbuffer))
        {

            if (!strcmp(ImageName, PsGetImageName(pbuffer)))
            {
                targetProcess = pbuffer;

                found = 1;

                break;
            }

            ObDereferenceObject(pbuffer);


        }

        if (!sysinfo->NextEntryOffset) break;

        sys += sysinfo->NextEntryOffset;

        sysinfo = (struct SYSTEM_PROCESS_INFORMATION*)sys;

    }

    ExFreePool((PVOID)zero);

    if (!found) return STATUS_INVALID_PARAMETER_1;

    return STATUS_SUCCESS;
}

NTSTATUS KeyboardApc(void *a1, void *a2, void *a3, void *a4, void *a5)
{
    unsigned char max = (unsigned char)mjRead->MakeCode;


    if (!mjRead->Flags)
    {
        KEY_DATA[(max)-1] = 1;
    }
    else if (mjRead->Flags&KEY_BREAK)
    {
        KEY_DATA[(max)-1] = 0;
    }

    return KeyboardInputRoutine(a1, a2, a3, a4, a5);
}

NTSTATUS MouseApc(void *a1, void *a2, void *a3, void *a4, void *a5)
{
    if (mouIrp->ButtonFlags&MOUSE_LEFT_BUTTON_DOWN)
    {
        MOU_DATA[0] = 1;
    }
    else if (mouIrp->ButtonFlags&MOUSE_LEFT_BUTTON_UP)
    {
        MOU_DATA[0] = 0;
    }
    else if (mouIrp->ButtonFlags&MOUSE_RIGHT_BUTTON_DOWN)
    {
        MOU_DATA[1] = 1;
    }
    else if (mouIrp->ButtonFlags&MOUSE_RIGHT_BUTTON_UP)
    {
        MOU_DATA[1] = 0;
    }
    else if (mouIrp->ButtonFlags&MOUSE_MIDDLE_BUTTON_DOWN)
    {
        MOU_DATA[2] = 1;
    }
    else if (mouIrp->ButtonFlags&MOUSE_MIDDLE_BUTTON_UP)
    {
        MOU_DATA[2] = 0;
    }
    else if (mouIrp->ButtonFlags&MOUSE_BUTTON_4_DOWN)
    {
        MOU_DATA[3] = 1;
    }
    else if (mouIrp->ButtonFlags&MOUSE_BUTTON_4_UP)
    {
        MOU_DATA[3] = 0;
    }
    else if (mouIrp->ButtonFlags&MOUSE_BUTTON_5_DOWN)
    {
        MOU_DATA[4] = 1;
    }
    else if (mouIrp->ButtonFlags&MOUSE_BUTTON_5_UP)
    {
        MOU_DATA[4] = 0;
    }

    return MouseInputRoutine(a1, a2, a3, a4, a5);
}


NTSTATUS ReadInstrumentation(PDEVICE_OBJECT device, PIRP irp)
{
    ULONGLONG *routine;

    routine = (ULONGLONG*)irp;

    routine += 0xb;


    if (!KeyboardInputRoutine)
    {
        KeyboardInputRoutine = (kbdinput)*routine;
    }

    *routine = (ULONGLONG)KeyboardApc;

    mjRead = (PKEYBOARD_INPUT_DATA)irp->UserBuffer;

    return KbdClassReadRoutine(device, irp);
}

NTSTATUS ReadInstrumentation1(PDEVICE_OBJECT device, PIRP irp)
{
    ULONGLONG *routine;

    routine = (ULONGLONG*)irp;

    routine += 0xb;


    if (!MouseInputRoutine)
    {
        MouseInputRoutine = (mouinput)*routine;
    }

    *routine = (ULONGLONG)MouseApc;

    mouIrp = (PMOUSE_INPUT_DATA)irp->UserBuffer;

    return MouClassReadRoutine(device, irp);
}


NTSTATUS Edox_InternalIoctl(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION ios;
    PCONNECT_DATA cd;

    ios = IoGetCurrentIrpStackLocation(irp);

    if (ios->Parameters.DeviceIoControl.IoControlCode == MOUCLASS_CONNECT_REQUEST)
    {
        cd = (PCONNECT_DATA)ios->Parameters.DeviceIoControl.Type3InputBuffer;

        MouseDpcRoutine = (MouseServiceDpc)cd->ClassService;
    }
    else if (ios->Parameters.DeviceIoControl.IoControlCode == KBDCLASS_CONNECT_REQUEST)
    {
        cd = (PCONNECT_DATA)ios->Parameters.DeviceIoControl.Type3InputBuffer;

        KeyboardDpcRoutine = (KeyboardServiceDpc)cd->ClassService;
    }
    else
    {
        Edox_InvalidRequest(device, irp);
    }

    return STATUS_SUCCESS;
}

NTSTATUS Edox_InvalidRequest(PDEVICE_OBJECT device, PIRP irp)
{
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

void *FindDevNodeRecurse(PDEVICE_OBJECT a1, ULONGLONG *a2)
{
    struct DEVOBJ_EXTENSION_FIX *attachment;

    attachment = (struct DEVOBJ_EXTENSION_FIX*)a1->DeviceObjectExtension;

    if ((!attachment->AttachedTo) && (!attachment->DeviceNode)) return;

    if ((!attachment->DeviceNode) && (attachment->AttachedTo))
    {
        FindDevNodeRecurse(attachment->AttachedTo, a2);

        return;
    }

    *a2 = (ULONGLONG)attachment->DeviceNode;

    return;
}

ULONG filter(void* a1)
{
    return 0;
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT driverObject, IN PUNICODE_STRING regPath)
{
    UNICODE_STRING symbolicLink;
    UNICODE_STRING deviceName;
    PDEVICE_OBJECT devicePtr;
    int i = 0;
    PDEVICE_OBJECT input_mouse;
    PDEVICE_OBJECT input_keyboard;
    CLIENT_ID nthread;
    PKTHREAD p = NULL;
    PEXCEPTION_POINTERS ptr;


    /*==============================*/
    UNICODE_STRING classNameBuffer;
    UNICODE_STRING routineName;
    PDEVICE_OBJECT classObj;
    PFILE_OBJECT file;
    PDRIVER_OBJECT classDrv;
    MouseAddDevice MouseAddDevicePtr;
    KeyboardAddDevice KeyboardAddDevicePtr;
    struct DEVOBJ_EXTENSION_FIX *DevObjExtension;
    ULONGLONG node = 0;
    wchar_t *u;
    USHORT charBuff;
    wchar_t kbdname[23] = L"\\Device\\KeyboardClass0";
    wchar_t mouname[22] = L"\\Device\\PointerClass0";
    void *linear = (void*)ReadInstrumentation;
    HANDLE thread;
    /*==============================*/

    memset((void*)&mdata, 0, sizeof(mdata));
    memset((void*)&kdata, 0, sizeof(kdata));
    memset((void*)MOU_DATA, 0, sizeof(MOU_DATA));

    RtlInitUnicodeString(&routineName, L"MmCopyVirtualMemory");

    MmCopyVirtualMemoryRoutine = (MmCopyVirtualMemory)MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"ZwQuerySystemInformation");

    ZwQuerySystemInformation = (NtQuerySystemInformation)MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"PsGetProcessImageFileName");

    PsGetImageName = (PsGetProcessImageFileName)MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"PsGetProcessPeb");

    PsGetPeb64 = (PsGetProcessPeb)MmGetSystemRoutineAddress(&routineName);

    RtlInitUnicodeString(&routineName, L"PsGetProcessWow64Process");

    PsGetPeb32 = (PsGetProcessWow64Process)MmGetSystemRoutineAddress(&routineName);

    /**/
    //RtlInitUnicodeString(&deviceName,L"\\Device\\edoxHID");

    IoCreateDevice(driverObject, 0, NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &devicePtr);

    //RtlInitUnicodeString(&symbolicLink,L"\\DosDevices\\mkInput");

    //IoCreateSymbolicLink(&symbolicLink,&deviceName);

    /**/

    RtlInitUnicodeString(&deviceName, L"\\Device\\edoxMouse");

    IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &input_mouse);

    RtlInitUnicodeString(&deviceName, L"\\Device\\edoxKeyboard");

    IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &input_keyboard);


    for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
        driverObject->MajorFunction[i] = Edox_InvalidRequest;

    driverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = Edox_InternalIoctl;
    driverObject->MajorFunction[IRP_MJ_READ] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_CREATE] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_CLOSE] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_CLEANUP] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_POWER] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Edox_InvalidRequest;
    driverObject->MajorFunction[IRP_MJ_PNP] = Edox_InvalidRequest;

    driverObject->DriverUnload = UnloadDriver;

    devicePtr->Flags |= DO_BUFFERED_IO;
    devicePtr->Flags &= ~DO_DEVICE_INITIALIZING;

    input_mouse->Flags |= DO_BUFFERED_IO;
    input_mouse->Flags &= ~DO_DEVICE_INITIALIZING;

    input_keyboard->Flags |= DO_BUFFERED_IO;
    input_keyboard->Flags &= ~DO_DEVICE_INITIALIZING;

    /*==============================*/

    //find a devnode we can hijack

    RtlInitUnicodeString(&classNameBuffer, mouname);

    u = mouname;


    while (1)
    {
        //run till we run out of devices or find a devnode

        if (IoGetDeviceObjectPointer(&classNameBuffer, FILE_ALL_ACCESS, &file, &classObj)) return STATUS_OBJECT_NAME_NOT_FOUND;

        ObDereferenceObject(file);

        node = (ULONGLONG)FindDevNodeRecurse(classObj, &node);

        if (node) break;

        *(u + MOU_STRING_INC) += 1;

        mouId++;

    }

    mouTarget = classObj;

    classDrv = classObj->DriverObject;

    MouClassReadRoutine = (MouclassRead)classDrv->MajorFunction[IRP_MJ_READ];

    classDrv->MajorFunction[IRP_MJ_READ] = ReadInstrumentation1;

    DevObjExtension = (struct DEVOBJ_EXTENSION_FIX*)input_mouse->DeviceObjectExtension;

    DevObjExtension->DeviceNode = (void*)node;

    MouseAddDevicePtr = (MouseAddDevice)classDrv->DriverExtension->AddDevice;

    MouseAddDevicePtr(classDrv, input_mouse);

    //repeat same process for keyboard stacks

    RtlInitUnicodeString(&classNameBuffer, kbdname);

    u = kbdname;


    charBuff = *(u + KBD_STRING_INC);


    while (1)
    {
        //run till we run out of devices or find a devnode

        if (IoGetDeviceObjectPointer(&classNameBuffer, FILE_ALL_ACCESS, &file, &classObj)) return STATUS_OBJECT_NAME_NOT_FOUND;

        ObDereferenceObject(file);

        node = (ULONGLONG)FindDevNodeRecurse(classObj, &node);

        if (node) break;

        *(u + KBD_STRING_INC) += 1;

        kbdId++;

    }


    *(u + KBD_STRING_INC) = charBuff;

    kbdTarget = classObj;

    classDrv = classObj->DriverObject;

    DevObjExtension = (struct DEVOBJ_EXTENSION_FIX*)input_keyboard->DeviceObjectExtension;

    DevObjExtension->DeviceNode = (void*)node;

    KeyboardAddDevicePtr = (KeyboardAddDevice)classDrv->DriverExtension->AddDevice;

    KeyboardAddDevicePtr(classDrv, input_keyboard);

    /**/
    KbdClassReadRoutine = (KbdclassRead)classDrv->MajorFunction[IRP_MJ_READ];

    classDrv->MajorFunction[IRP_MJ_READ] = ReadInstrumentation;

    for (i = 0; i < 128; i++) KEY_DATA[i] = 0;

    PsCreateSystemThread(&thread, STANDARD_RIGHTS_ALL, NULL, NULL, &nthread, (PKSTART_ROUTINE)SystemRoutine, NULL);


    ZwClose(thread);




    return STATUS_SUCCESS;
}

NTSTATUS UnloadDriver(PDRIVER_OBJECT driver)
{
    IoDeleteDevice(driver->DeviceObject);
    return STATUS_SUCCESS;
}
