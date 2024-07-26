#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>
#pragma comment(lib, "hidclass.lib")
#define GET_NEXT_DEVICE_OBJECT(DO) \
    (((PHID_DEVICE_EXTENSION)(DO)->DeviceExtension)->NextDeviceObject)

DRIVER_INITIALIZE   DriverEntry;
DRIVER_ADD_DEVICE   HidKmdfAddDevice;
DRIVER_DISPATCH     HidKmdfPassThrough;
DRIVER_DISPATCH     HidKmdfPowerPassThrough;
DRIVER_UNLOAD       DriverUnload;

#ifdef ALLOC_PRAGMA
#pragma alloc_text( INIT, DriverEntry )
#pragma alloc_text( PAGE, HidKmdfAddDevice)
#pragma alloc_text( PAGE, DriverUnload)
#endif

NTSTATUS 
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    DbgPrint("Hello World\n");

    HID_MINIDRIVER_REGISTRATION hidMinidriverRegistration;
    NTSTATUS status;
    ULONG i;

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = HidKmdfPassThrough;
    }

    DriverObject->MajorFunction[IRP_MJ_POWER] = HidKmdfPowerPassThrough;

    DriverObject->DriverExtension->AddDevice = HidKmdfAddDevice;
    DriverObject->DriverUnload = DriverUnload;

    RtlZeroMemory(&hidMinidriverRegistration,
        sizeof(hidMinidriverRegistration));

    hidMinidriverRegistration.Revision = HID_REVISION;
    hidMinidriverRegistration.DriverObject = DriverObject;
    hidMinidriverRegistration.RegistryPath = RegistryPath;
    hidMinidriverRegistration.DeviceExtensionSize = 0;

    hidMinidriverRegistration.DevicesArePolled = FALSE;

    status = HidRegisterMinidriver(&hidMinidriverRegistration);
    if (!NT_SUCCESS(status)) {
        KdPrint(("HidRegisterMinidriver FAILED, returnCode=%x\n", status));
    }

    return status;
}

NTSTATUS
HidKmdfAddDevice(
    __in PDRIVER_OBJECT DriverObject,
    __in PDEVICE_OBJECT FunctionalDeviceObject
)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    FunctionalDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

NTSTATUS
HidKmdfPassThrough(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(GET_NEXT_DEVICE_OBJECT(DeviceObject), Irp);
}

NTSTATUS
HidKmdfPowerPassThrough(
    __in PDEVICE_OBJECT DeviceObject,
    __in PIRP Irp
)
{
 
    PoStartNextPowerIrp(Irp);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    return PoCallDriver(GET_NEXT_DEVICE_OBJECT(DeviceObject), Irp);
}



VOID DriverUnload(__in PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    DbgPrint("Goodbye!\n");
    KdPrint(("Thanks For Using!\n"));
    PAGED_CODE();

    return;
}
