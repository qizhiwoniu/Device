#include <joystick.h>
#include <string.h>
#include <stdlib.h>
#include <rawpdo.h>


#if defined(EVENT_TRACING)
//
// The trace message header (.tmh) file must be included in a source file
// before any WPP macro calls and after defining a WPP_CONTROL_GUIDS
// macro (defined in toaster.h). During the compilation, WPP scans the source
// files for DoTraceMessage() calls and builds a .tmh file which stores a unique
// data GUID for each message, the text resource string for each message,
// and the data types of the variables passed in for each message.  This file
// is automatically generated and used during post-processing.
//
#include "driver.tmh"
#else
ULONG DebugLevel = TRACE_LEVEL_VERBOSE;
ULONG DebugFlag = 0xff;
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text( INIT, DriverEntry )
#pragma alloc_text( PAGE, JoystickEvtDeviceAdd)
#endif


NTSTATUS
DriverEntry(
    __in PDRIVER_OBJECT  DriverObject,
    __in PUNICODE_STRING RegistryPath
)
{

    NTSTATUS				status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG		config;
    //WDF_OBJECT_ATTRIBUTES	attributes;
    WDFDRIVER				hDriver;

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Joystick Driver Built %s %s\n", __DATE__, __TIME__);
    // KdBreakPoint(); Break at the entry point to the driver, use for debug only

    // To build a single driver binary that runs both in Windows 8 and in earlier versions of Windows, use the POOL_NX_OPTIN opt-in mechanism. 
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    WDF_DRIVER_CONFIG_INIT(&config, JoystickEvtDeviceAdd);

    status = WdfDriverCreate(DriverObject,
        RegistryPath,
        /*&attributes*/ WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        &hDriver);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfDriverCreate failed with status 0x%x\n", status));
        LogEventWithStatus(ERRLOG_DRIVER_FAILED, L"WdfDriverCreate", DriverObject, status);
        return status;
    };


    status = WdfCollectionCreate(WDF_NO_OBJECT_ATTRIBUTES, &JoystickDeviceCollection);
    if (!NT_SUCCESS(status)) {
        KdPrint(("WdfCollectionCreate failed with status 0x%x\n", status));
        LogEventWithStatus(ERRLOG_DRIVER_FAILED, L"WdfCollectionCreate", DriverObject, status);
        return status;
    }


    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &JoystickDeviceCollectionLock);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfWaitLockCreate(JoystickDeviceCollectionLock) failed with status 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DRIVER_FAILED, L"WdfWaitLockCreate (1)", DriverObject, status);
        return status;
    }

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_DeviceCounterLock);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfWaitLockCreate(g_DeviceCounterLock) failed with status 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DRIVER_FAILED, L"WdfWaitLockCreate (2)", DriverObject, status);
        return status;
    }

    // Reset device counter to 0
    if (-1 > DeviceCount(FALSE, 0)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "DeviceCount initialization failed\n");
        LogEvent(ERRLOG_DRIVER_FAILED1, NULL, DriverObject);
        return STATUS_DRIVER_INTERNAL_ERROR;
    }

    LogEvent(ERRLOG_DRIVER_INSTALLED, NULL, DriverObject);
    return status;
}


NTSTATUS
JoystickEvtDeviceAdd(
    IN WDFDRIVER       Driver,
    IN PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS                      status = STATUS_SUCCESS;
    WDF_IO_QUEUE_CONFIG           queueConfig;
    WDF_OBJECT_ATTRIBUTES         attributes, FfbQueueAttribs;
    WDFDEVICE                     hDevice;
    PDEVICE_EXTENSION             devContext = NULL;
    WDFQUEUE                      queue;
    //WDF_PNPPOWER_EVENT_CALLBACKS  pnpPowerCallbacks;
    WDF_TIMER_CONFIG              timerConfig;

    DECLARE_CONST_UNICODE_STRING(CompatId, COMPATIBLE_DEVICE_ID);
    DECLARE_CONST_UNICODE_STRING(DeviceId, Joystick_DEVICE_ID);
    DECLARE_CONST_UNICODE_STRING(InstanceId, Joystick_INSTANCE_ID);
    //WDFSTRING  stringHandle = NULL;
    //UNICODE_STRING uStr;
    //TCHAR						DeviceName[100];
    LONG						SerialNumber = -1;
    PDEVICE_OBJECT				fdoDeviceObject = NULL;
    WDF_IO_TARGET_OPEN_PARAMS	openParams;
    WDFIOTARGET					ioTarget;
    PFFB_QUEUE_EXTENSION		queueContext = NULL;
    int 						i;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "JoystickEvtDeviceAdd: entering\n");
    //KdBreakPoint();

#if 0
    status = GetHwKeySerialNumber(DeviceInit, &SerialNumber);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "GetHwKeySerialNumber failed with status code 0x%x\n", status);
        return status;
    }
    if (SerialNumber > 0) {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "GetHwKeySerialNumber returned Serial Number %d - JoystickEvtDeviceAdd aborting\n", SerialNumber);
        return STATUS_UNSUCCESSFUL;
    }
#else
    SerialNumber = DeviceCount(TRUE, 1);
    if (-1 > SerialNumber) {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "JoystickEvtDeviceAdd: DeviceCount Failed- aborting\n");
        return STATUS_UNSUCCESSFUL;
    }
   
#endif

    WdfFdoInitSetFilter(DeviceInit);

    // Child device's compatible ID is "hid_device_system_game" 子设备的兼容ID为“hid_device_system_game”
    // Additional ones may be added below
    status = WdfPdoInitAddCompatibleID(DeviceInit, &CompatId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "JoystickEvtDeviceAdd: WdfPdoInitAddCompatibleID failed with status code 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfPdoInitAddCompatibleID", NULL, status);
    }

    status = WdfPdoInitAssignDeviceID(DeviceInit, &DeviceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "JoystickEvtDeviceAdd: WdfPdoInitAssignDeviceID failed with status code 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfPdoInitAssignDeviceID", NULL, status);
    }

    status = WdfPdoInitAssignInstanceID(DeviceInit, &InstanceId);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_PNP, "JoystickEvtDeviceAdd: WdfPdoInitAssignInstanceID failed with status code 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfPdoInitAssignInstanceID", NULL, status);
    }

    //WdfDeviceInitAssignSDDLString(DeviceInit,
    //                                       &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_R_RES_R);



    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_EXTENSION);
    attributes.EvtCleanupCallback = (PFN_WDF_OBJECT_CONTEXT_CLEANUP)JoystickEvtDeviceContextCleanup; // See https://social.msdn.microsoft.com/Forums/en-US/ba0e4557-05a7-42a0-a960-cf2ded57ecfb/driver-analysis-undocumented-warning-c28118?forum=wdk

#if 0
    // Test with PNP requests
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDeviceReleaseHardware = JoystickEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDevicePrepareHardware = JoystickEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceSelfManagedIoFlush = JoystickEvtDeviceSelfManagedIoFlush;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    // Test with PNP State Machine requests
    WdfDeviceInitRegisterPnpStateChangeCallback(DeviceInit, WdfDevStatePnpStopped, JoystickEvtDevicePnpStateChange, StateNotificationAllStates);

#endif 
   
    status = WdfDeviceCreate(&DeviceInit, &attributes, &hDevice);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfDeviceCreate failed with status code 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfDeviceCreate", NULL, status);
        return status;
    }

    // Get the pointer to the device context structure of type DEVICE_EXTENSION
    devContext = GetDeviceContext(hDevice);
    InitializeDeviceContext(devContext);

    ///////////////  Create Wait-Lock Object ///////////////////////////////////////
    // Create a wait-lock object that will be used to synch access to positions[i]
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = hDevice;
    status = WdfWaitLockCreate(&attributes, &(devContext->positionLock));
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "JoystickEvtDeviceAdd: WdfWaitLockCreate failed 0x%x\n", status);
        LogEventWithStatus(ERRLOG_RAW_DEV_FAILED, L"WdfWaitLockCreate", WdfDriverWdmGetDriverObject(WdfGetDriver()), status);
        return status;
    }

    InitializeDefaultDev(devContext);

    ///////////// Default queue that handles Internal Device Control IRPs ////////////////////
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = JoystickEvtInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoQueueCreate failed to create Internal Device Control Queue 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoQueueCreate (1)", WdfDeviceWdmGetDeviceObject(hDevice), status);
        return status;
    }
    /////////////////////////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////////////////////////
    //
    // Register a manual I/O queue for handling Interrupt Message Read Requests.
    // This queue will be used for storing Requests that need to wait for an
    // interrupt to occur before they can be completed.
    //
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

    //
    // This queue is used for requests that dont directly access the device. The
    // requests in this queue are serviced only when the device is in a fully
    // powered state and sends an interrupt. So we can use a non-power managed
    // queue to park the requests since we dont care whether the device is idle
    // or fully powered up.
    //
    queueConfig.PowerManaged = WdfFalse/*WdfTrue*/;
    status = WdfIoQueueCreate(hDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &devContext->ReadReportMsgQueue);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoQueueCreate failed 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoQueueCreate (2)", WdfDeviceWdmGetDeviceObject(hDevice), status);
        return status;
    }
    /////////////////////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////// FFB ///////////////////////////////////////
    //

    //
    // Register a manual I/O queue for handling Interrupt Message Write and Set Feature Requests.
    // This queue will be used for storing Requests that need to wait for an application to read them
    // They are written to the driver by the client application (DirectInput)
    //

    // Initialize the FFB enable-bit of all joysticks - Disable them all
    FfbDisableAll(devContext);


    // Create a set of FFB manual Write queues
    WDF_OBJECT_ATTRIBUTES_INIT(&FfbQueueAttribs);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&FfbQueueAttribs, FFB_QUEUE_EXTENSION);
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse/*WdfTrue*/;

    // For each device, create a Write Queue, initialize its context space and assign notification
    for (i = 0; i < Joystick_MAX_N_DEVICES; i++) {
        status = WdfIoQueueCreate(hDevice, &queueConfig, &FfbQueueAttribs, &devContext->FfbWriteQ[i]);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoQueueCreate failed[id=%d] 0x%x\n", i + 1, status);
            LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoQueueCreate (3)", WdfDeviceWdmGetDeviceObject(hDevice), status);
            return status;
        };

        // Get the context space of the new queue
        queueContext = GetFfbQueuContext(devContext->FfbWriteQ[i]);
        if (queueContext) {
            queueContext->DeviceID = i + 1;
            queueContext->isRead = FALSE;
        }


        // Register a Notification function for this queue
        // Whenever a the queue is ready this function starts
        status = WdfIoQueueReadyNotify(devContext->FfbWriteQ[i], FfbNotifyWrite, devContext);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoQueueReadyNotify failed[id=%d] 0x%x\n", i + 1, status);
            LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoQueueReadyNotify (Write)", WdfDeviceWdmGetDeviceObject(hDevice), status);
            return status;
        }

    } // For each device, create a Write Queue, initialize its context space and assign notification

    // Register a manual I/O queue for handling Interrupt Messages GET_FFB_DATA
    // This queue will be used for storing Requests that need to wait until FFB data is available
    //
    // Create a set of FFB manual Read queues
    WDF_OBJECT_ATTRIBUTES_INIT(&FfbQueueAttribs);
    WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&FfbQueueAttribs, FFB_QUEUE_EXTENSION);
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    queueConfig.PowerManaged = WdfFalse/*WdfTrue*/;

    for (i = 0; i < Joystick_MAX_N_DEVICES; i++) {
        status = WdfIoQueueCreate(hDevice, &queueConfig, &FfbQueueAttribs, &devContext->FfbReadQ[i]);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoQueueCreate failed[id=%d] 0x%x\n", i + 1, status);
            LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoQueueCreate (3)", WdfDeviceWdmGetDeviceObject(hDevice), status);
            return status;
        }

        // Get the context space of the new queue
        queueContext = GetFfbQueuContext(devContext->FfbReadQ[i]);
        if (queueContext) {
            queueContext->DeviceID = i + 1;
            queueContext->isRead = TRUE;
        }


        // Register a Notification function for this queue
        // Whenever a the queue is ready this function starts
        status = WdfIoQueueReadyNotify(devContext->FfbReadQ[i], FfbNotifyRead, devContext);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoQueueReadyNotify failed[id=%d] 0x%x\n", i + 1, status);
            LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoQueueReadyNotify (Read)", WdfDeviceWdmGetDeviceObject(hDevice), status);
            return status;
        }
    } 	// For each device, create a Read Queue, initialize its context space and assign notification

    // Create a wait lock that ensure a single contemporary inter-queue transfer
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &(devContext->FfbXferLock));
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfWaitLockCreate failed 0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfWaitLockCreate (FfbXferLock)", WdfDeviceWdmGetDeviceObject(hDevice), status);
        return status;
    }

    /////////////////////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////  Raw PDO ///////////////////////////////////////
    //
    //
    // create and open an I/O target that represents the parent bus FDO

    // Step 1. Get a pointer to the WDM device object for parent bus	
    fdoDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice); // Parent bus FDO

    //Step 2. Create the I/O target object.
    status = WdfIoTargetCreate(hDevice, WDF_NO_OBJECT_ATTRIBUTES, &ioTarget);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoTargetCreate failed status:0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoTargetCreate", fdoDeviceObject, status);
        return status;
    }

    //Step 3. Initialize the open params structure.
    WDF_IO_TARGET_OPEN_PARAMS_INIT_EXISTING_DEVICE(&openParams, fdoDeviceObject);

    //Step 4. Open the I/O target.
    status = WdfIoTargetOpen(ioTarget, &openParams);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfIoTargetOpen failed status:0x%x\n", status);
        LogEventWithStatus(ERRLOG_DEVICE_FAILED, L"WdfIoTargetOpen", fdoDeviceObject, status);
        return status;
    }

    //Step 5. Save the I/O target handle.
    devContext->IoTargetToSelf = ioTarget;

    //
    //
    //
    // Create a RAW pdo so we can provide a sideband communication with
    // the application. Please note that not filter drivers desire to
    // produce such a communication and not all of them are contrained
    // by other filter above which prevent communication thru the device
    // interface exposed by the main stack. So use this only if absolutely
    // needed. Also look at the toaster filter driver sample for an alternate
    // approach to providing sideband communication.
    //
    status = Joystick_CreateRawPdo(hDevice, SerialNumber);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: Joystick_CreateRawPdo failed status:0x%x\n", status);
    }


    /////////////////////////////////////////////////////////////////////////////////////////
    //
    //	Create a timer that completes IOCTL_HID_READ_REPORT pending requests
    //	Calback function will be called by this timer every Joystick_READ_REPORT_PERIOD (=500) mS
    //
    WDF_TIMER_CONFIG_INIT(&timerConfig, JoystickEvtTimerFunction);
    timerConfig.AutomaticSerialization = FALSE;
    timerConfig.Period = Joystick_READ_REPORT_PERIOD;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = hDevice;
    status = WdfTimerCreate(&timerConfig, &attributes, &(devContext->timerHandle));
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "JoystickEvtDeviceAdd: WdfTimerCreate failed status:0x%x\n", status);
        return status;
    }
    // Temporarily remove the timer - this will disable the periodic data update
    WdfTimerStart(devContext->timerHandle, 100);
    /////////////////////////////////////////////////////////////////////////////////////////

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "JoystickEvtDeviceAdd: exiting\n");


    LogEvent(ERRLOG_DEVICE_ADDED, NULL, fdoDeviceObject);
    return status;
}

VOID
JoystickEvtTimerFunction(
    IN WDFTIMER  Timer
)
{
    static UCHAR i = 1;

    //
    // Complete the request
    if (i <= Joystick_MAX_N_DEVICES) {
        JoystickCompleteReadReport(WdfTimerGetParentObject(Timer), i++);
        return;
    }
    else
        i = 1;
}

#if 0
/*
 NOT USED!
 Has been deferred to user code handling.
*/
NTSTATUS
JoystickCompleteWriteReport(
    WDFREQUEST request
)
{
    NTSTATUS	status = STATUS_SUCCESS;
    ULONG		bytesToCopy = 0;
    size_t		bytesReturned = 0;
    UCHAR		ucBuffer[20] = { 0 };
    WDF_REQUEST_PARAMETERS Parameters;
    PHID_XFER_PACKET transferPacket = NULL;
    UCHAR		EffectBlockIndex = 0;
    short		Magnitude = 0;


    WDF_REQUEST_PARAMETERS_INIT(&Parameters);
    WdfRequestGetParameters(request, &Parameters);
    bytesToCopy = (ULONG)Parameters.Parameters.DeviceIoControl.InputBufferLength;

    transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(request)->UserBuffer;

    if (transferPacket == NULL) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        return status;
    };

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "JoystickCompleteWriteReport: entering with reportId=%d\n", transferPacket->reportId);

    // Switch By report ID
    switch (transferPacket->reportId) {
    case 1:
        break;
    case 2:
        break;
    case 5:
        EffectBlockIndex = ((PUCHAR)(transferPacket->reportBuffer))[1];
        Magnitude = ((short*)(transferPacket->reportBuffer))[1];
        break;
    default:
        break;
    }; // End switch

    if (bytesToCopy)
        status = WdfRequestRetrieveInputBuffer(request, bytesToCopy, (PVOID*)&ucBuffer, &bytesReturned);
    WdfRequestCompleteWithInformation(request, status, bytesReturned);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "JoystickCompleteWriteReport: exiting with stt=0x%d\n", status);

    return status;
}
#endif

#if 0
NTSTATUS
JoystickWriteReport(
    IN WDFDEVICE device,
    IN WDFREQUEST Request
)
{
    PDEVICE_EXTENSION   pDevContext = NULL;
    NTSTATUS            status = STATUS_SUCCESS;
    //WDFQUEUE			WriteQueue = NULL;
    PHID_XFER_PACKET	transferPacket = NULL;
    WDFREQUEST			FfbRequest = NULL;
    PVOID				ForceFeedbackBuffer = NULL;
    size_t				bytesReturned = 0;


    // Get the device context area - this is the starting point
    pDevContext = GetDeviceContext(device);
    if (!pDevContext)
        return STATUS_INVALID_PARAMETER;


    // Get the FFB data from the request
    transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;
    if (transferPacket == NULL) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        WdfRequestComplete(FfbRequest, status);
        return status;
    };


    status = WdfRequestRetrieveOutputBuffer(FfbRequest, transferPacket->reportBufferLen, &ForceFeedbackBuffer, &bytesReturned);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(FfbRequest, status);
        return status;
    }

    // Copy the report to target read queue
    memcpy(ForceFeedbackBuffer, transferPacket->reportBuffer, transferPacket->reportBufferLen);
    WdfRequestCompleteWithInformation(FfbRequest, status, bytesReturned);

    return status;
}


#endif // 0

NTSTATUS
JoystickCompleteReadReport(
    WDFDEVICE Device,
    UCHAR id
)

{
    NTSTATUS             status = STATUS_SUCCESS;
    WDFREQUEST           request;
    PDEVICE_EXTENSION    pDevContext = NULL;
    size_t               bytesReturned = 0;
    //UCHAR                toggledSwitch = 0;
    ULONG                bytesToCopy = 0;
    PHID_INPUT_REPORT    HidReport = NULL;

    //WDFMEMORY           memory;
    //size_t				NumBytesTransferred;
    //PUCHAR				switchState = NULL;
    //UCHAR				eb;

    pDevContext = GetDeviceContext(Device);
    // Check if the requested report is valid. If not just return
    if (!pDevContext->positions[id - 1])
        return STATUS_INVALID_PARAMETER;

    // Test if device exists if not, return STATUS_NO_SUCH_DEVICE
    if (!pDevContext->DeviceImplemented[id - 1])
        return STATUS_NO_SUCH_DEVICE;

    // Test if device "Dirty bit" is set - if not, return STATUS_INVALID_DEVICE_REQUEST
    // Dirty Bit is set when new data is loaded to the position structure of a Joystick device
    // and reset after data was read.
    if (!pDevContext->PositionReady[id - 1])
        return STATUS_INVALID_DEVICE_REQUEST;


    //
    // Check if there are any pending requests in the Read Report Interrupt Message Queue.
    // If a request is found then complete the pending request.
    //
    status = WdfIoQueueRetrieveNextRequest(pDevContext->ReadReportMsgQueue, &request);

    if (NT_SUCCESS(status)) {
        //
        // IOCTL_HID_READ_REPORT is METHOD_NEITHER so WdfRequestRetrieveOutputBuffer
        // will correctly retrieve buffer from Irp->UserBuffer. Remember that
        // HIDCLASS provides the buffer in the Irp->UserBuffer field
        // irrespective of the ioctl buffer type. However, framework is very
        // strict about type checking. You cannot get Irp->UserBuffer by using
        // WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
        // internal ioctl.
        //
        bytesToCopy = sizeof(HID_INPUT_REPORT);
        status = WdfRequestRetrieveOutputBuffer(request, bytesToCopy, &HidReport, &bytesReturned);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "WdfRequestRetrieveOutputBuffer failed with status: 0x%x\n", status);
        }
        else {
            // Copy the data stored in the Device Context into the the output buffer
            if (JoystickGetPositionData(HidReport, pDevContext, id, bytesReturned) != STATUS_SUCCESS)
                status = STATUS_UNSUCCESSFUL;
        }

        WdfRequestCompleteWithInformation(request, status, bytesReturned);

    }
    else {
        if (status == STATUS_INVALID_DEVICE_STATE)
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfIoQueueRetrieveNextRequest status STATUS_INVALID_DEVICE_STATE\n");
        if (status != STATUS_NO_MORE_ENTRIES)
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "WdfIoQueueRetrieveNextRequest status %08x\n", status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, "JoystickCompleteReadReport: exiting with stt=0x%x\n", status);

    return status;
}

NTSTATUS
JoystickGetPositionData(
    OUT HID_INPUT_REPORT* HidReport,
    IN DEVICE_EXTENSION* pDevContext,
    IN UCHAR				id, // ID is one-based
    IN size_t				size
)
{
    int i;
    LONGLONG timeout = 0;
    UNREFERENCED_PARAMETER(size);

    // If id is out of range then assume id is 1
    if (id > Joystick_MAX_N_DEVICES || id < 1)
        id = 1;

    // id is one-based
    i = id - 1;

    if (!pDevContext->positions[i])
        return STATUS_ACCESS_VIOLATION;


    if (STATUS_SUCCESS == WdfWaitLockAcquire(pDevContext->positionLock, &timeout)) {
        HidReport->InputReport.CollectionId = id;
        HidReport->InputReport.bAxisX = pDevContext->positions[i]->ValX;
        HidReport->InputReport.bAxisY = pDevContext->positions[i]->ValY;
        HidReport->InputReport.bAxisZ = pDevContext->positions[i]->ValZ;
        HidReport->InputReport.bAxisRX = pDevContext->positions[i]->ValRX;
        HidReport->InputReport.bAxisRY = pDevContext->positions[i]->ValRY;
        HidReport->InputReport.bAxisRZ = pDevContext->positions[i]->ValRZ;
        HidReport->InputReport.bSlider = pDevContext->positions[i]->ValSlider;
        HidReport->InputReport.bDial = pDevContext->positions[i]->ValDial;

        HidReport->InputReport.bWheel = pDevContext->positions[i]->ValWheel;
        HidReport->InputReport.bAccelerator = pDevContext->positions[i]->ValAccelerator;
        HidReport->InputReport.bBrake = pDevContext->positions[i]->ValBrake;
        HidReport->InputReport.bClutch = pDevContext->positions[i]->ValClutch;
        HidReport->InputReport.bSteering = pDevContext->positions[i]->ValSteering;
        HidReport->InputReport.bAileron = pDevContext->positions[i]->ValAileron;
        HidReport->InputReport.bRudder = pDevContext->positions[i]->ValRudder;
        HidReport->InputReport.bThrottle = pDevContext->positions[i]->ValThrottle;

        HidReport->InputReport.bHats = pDevContext->positions[i]->ValHats;
        HidReport->InputReport.bHatsEx1 = pDevContext->positions[i]->ValHatsEx1;
        HidReport->InputReport.bHatsEx2 = pDevContext->positions[i]->ValHatsEx2;
        HidReport->InputReport.bHatsEx3 = pDevContext->positions[i]->ValHatsEx3;
        HidReport->InputReport.bButtons = (ULONG)pDevContext->positions[i]->ValButtons;

        // Support 128 buttons
        // DEBUGGING if (size >= sizeof(HID_INPUT_REPORT_V2))
        {
            HidReport->InputReport.bButtonsEx1 = (ULONG)pDevContext->positions[i]->ValButtonsEx1;
            HidReport->InputReport.bButtonsEx2 = (ULONG)pDevContext->positions[i]->ValButtonsEx2;
            HidReport->InputReport.bButtonsEx3 = (ULONG)pDevContext->positions[i]->ValButtonsEx3;
        };

        // Clear 'dearty bit'
        // This means that the data above has already been read and should not be read again
        pDevContext->PositionReady[i] = FALSE;

        WdfWaitLockRelease(pDevContext->positionLock);

        return STATUS_SUCCESS;
    };

    return STATUS_TIMEOUT;
}




VOID
JoystickEvtDriverContextCleanup(
    IN WDFOBJECT Driver
)

{
    /*	_IRQL_requires_max_(1);
        PAGED_CODE ();
     */   // UNREFERENCED_PARAMETER(Driver);

    WPP_CLEANUP(WdfDriverWdmGetDriverObject(Driver));
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Exit JoystickEvtDriverContextCleanup\n");
    LogEvent(ERRLOG_DRIVER_REMOVED, NULL, WdfDriverWdmGetDriverObject(Driver));
}


UCHAR GetDeviceCount(PWSTR RegistryPathStr)
{
    NTSTATUS				status = STATUS_SUCCESS;
    WDFKEY					SubKey, Key;
    ULONG					DeviceCount = 0;
    UNICODE_STRING			EnumUnStr, CountUnStr, RegistryPath;

    // Create a registry string


    // Get the driver registry key
    RtlInitUnicodeString(&RegistryPath, RegistryPathStr);
    status = WdfRegistryOpenKey(NULL, &RegistryPath, GENERIC_READ, WDF_NO_OBJECT_ATTRIBUTES, &Key);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "GetDeviceCount: WdfRegistryOpenKey[1] failed with status 0x%x\n", status);
        return 0;
    };

    // Get the driver ENUM registry key
    RtlInitUnicodeString(&EnumUnStr, L"Enum");
    status = WdfRegistryOpenKey(Key, &EnumUnStr, GENERIC_READ, WDF_NO_OBJECT_ATTRIBUTES, &SubKey);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "GetDeviceCount: WdfRegistryOpenKey[2] failed with status 0x%x\n", status);
        WdfRegistryClose(Key);
        return 0;
    };

    // Get the device count
    RtlInitUnicodeString(&CountUnStr, L"Count");
    status = WdfRegistryQueryULong(SubKey, &CountUnStr, &DeviceCount);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "GetDeviceCount: WdfRegistryQueryValue[2] failed with status 0x%x\n", status);
        WdfRegistryClose(Key);
        WdfRegistryClose(SubKey);
        return 0;
    };


    WdfRegistryClose(Key);
    WdfRegistryClose(SubKey);
    return (UCHAR)DeviceCount;
}


int DeviceCount(BOOLEAN Relative, int Value)
{
    int PrevVal;
    NTSTATUS status = STATUS_SUCCESS;

    status = WdfWaitLockAcquire(g_DeviceCounterLock, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "DeviceCount failed with status code 0x%x\n", status);
        return -1;
    }

    PrevVal = g_DeviceCounter;
    if (Relative)
        g_DeviceCounter += Value;
    else
        g_DeviceCounter = Value;
    WdfWaitLockRelease(g_DeviceCounterLock);

    return PrevVal;
}

NTSTATUS GetHwKeyName(PWDFDEVICE_INIT  DeviceInit, PWCHAR HwKeyName)
{
    NTSTATUS				status = STATUS_SUCCESS;
    NTSTATUS				CloseHandle = STATUS_SUCCESS;
    WDFKEY					WdfHwKey;
    HANDLE					hHwKey;
    PKEY_BASIC_INFORMATION	infoHwKey;
    ULONG					Size;
    PWCHAR					backslash;

    // Get the WDFKEY of the HW Key for this device
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "GetHwKeyName calling WdfFdoInitOpenRegistryKey\n");
    status = WdfFdoInitOpenRegistryKey(DeviceInit, PLUGPLAY_REGKEY_DEVICE, GENERIC_READ, WDF_NO_OBJECT_ATTRIBUTES, &WdfHwKey);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "WdfFdoInitOpenRegistryKey failed with error %x\n", status);
        return status;
    };

    // Get the correspondig WDM handle
    hHwKey = WdfRegistryWdmGetHandle(WdfHwKey);
    DbgPrint("%p - Handle hHwKey for WdfHwKey\n", hHwKey);

    // Get the size of the name of the Device Parameters subkey under HW Key
    status = ZwQueryKey(hHwKey, KeyNameInformation, NULL, 0, &Size);
    if (STATUS_BUFFER_TOO_SMALL != status) {
        CloseHandle = ZwClose(hHwKey);
        hHwKey = NULL;
        DbgPrint("%p - Handle hHwKey was closed. Status %x [1]\n", hHwKey, CloseHandle);
        return status;
    }

    // Alocate buffer and get the name of the Device Parameters subkey under HW Key into it 
    infoHwKey = (PKEY_BASIC_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, Size + 1, 'AAA7');
    if (!infoHwKey)
        return STATUS_BUFFER_TOO_SMALL;

    status = ZwQueryKey(hHwKey, KeyNameInformation, infoHwKey, Size, &Size);
    if (!NT_SUCCESS(status)) {
        CloseHandle = ZwClose(hHwKey);
        hHwKey = NULL;
        DbgPrint("%p - Handle hHwKey was closed. Status %x [2]\n", hHwKey, CloseHandle);
        return status;
    }

    // Get the HW Key by going back one back-slash
    backslash = wcsrchr(infoHwKey->Name, L'\\');
    if (backslash)
        *backslash = L'\0';

    // Cleaning up, copying output string and out we go
    CloseHandle = ZwClose(hHwKey);
    hHwKey = NULL;
    //TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "GetHwKeyName: HwKey Name is %s\n",infoHwKey->Name);
    status = RtlStringCbCopyW(HwKeyName, 2 * (infoHwKey->NameLength), infoHwKey->Name);
    ExFreePoolWithTag(infoHwKey, 'AAA7');
    //if (!NT_SUCCESS(status))
    //{
    //	ZwClose(hHwKey);
    //	hHwKey = NULL;
    //}


    return status;

}

NTSTATUS GetHwKeySerialNumber(PWDFDEVICE_INIT  DeviceInit, PLONG SerialNumber)
{
    NTSTATUS				status = STATUS_SUCCESS;
    WCHAR					HwKeyName[260];
    PWSTR					leaf;
    ULONG					out;
    UNICODE_STRING			usLeaf;

    // Get the name of the HW key
    status = GetHwKeyName(DeviceInit, HwKeyName);
    if (!NT_SUCCESS(status))
        return status;

    // Extract the name of the leaf key:
    // Example: 
    //	If the HW key is HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\Root\HIDCLASS\0000
    //	then extract "0000"
    // Get the HW Key by going back one back-slash
    leaf = wcsrchr(HwKeyName, L'\\');
    if (!leaf)
        return STATUS_UNSUCCESSFUL;

    // Convert to number
    RtlInitUnicodeString(&usLeaf, &(leaf[1]));
    RtlUnicodeStringToInteger(&usLeaf, 10, &out);
    *SerialNumber = out;

    return status;

}

VOID FfbDisableAll(PDEVICE_EXTENSION devContext)
{
    size_t szarry, szelement, sz;
    size_t i;
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_FFB, "FfbDisableAll: entering\n");

    // Initialize all joysticks so they are not FFB capable.
    if (!devContext)
        return;

    szarry = sizeof(devContext->FfbEnable);
    szelement = sizeof(devContext->FfbEnable[0]);
    sz = szarry / szelement;

    for (i = 0; i < sz; i++)
        devContext->FfbEnable[i] = FALSE;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_FFB, "FfbDisableAll: exiting\n");
    return;
}

#pragma warning(disable: 4995)
VOID LogEvent(NTSTATUS code, PWSTR msg, PVOID pObj)
{

    PIO_ERROR_LOG_PACKET p;
    USHORT DumpDataSize = 0;
    ULONG packetlen = sizeof(IO_ERROR_LOG_PACKET) + DumpDataSize;
    if (msg)
        packetlen += (ULONG)((wcslen(msg) + 1) * sizeof(WCHAR));

    if (packetlen > ERROR_LOG_MAXIMUM_SIZE)
        return;
    // Allocate error log entry (will be free when sending)
    p = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(pObj, (UCHAR)packetlen);
    if (!p)
        return;

    memset(p, 0, sizeof(IO_ERROR_LOG_PACKET));
    p->ErrorCode = code;
    p->FinalStatus = 0;

    p->DumpDataSize = DumpDataSize;
    //p->DumpData[0] = <whatever>;

    p->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + p->DumpDataSize;

    if (msg) {
        p->NumberOfStrings = 1;
        // wcscpy((PWSTR)((PUCHAR)p + p->StringOffset), msg);
        wcscpy_s((PWSTR)((PUCHAR)p + p->StringOffset), (ULONG)((wcslen(msg) + 1)), msg); // TODO: Fix this line and remove the line above
    }
    else
        p->NumberOfStrings = 0;

    // Send error log to system's log and release error log entry
    IoWriteErrorLogEntry(p);
}

VOID LogEventWithStatus(NTSTATUS code, PWSTR msg, PVOID pObj, NTSTATUS stat)
{

    WCHAR  strStat[12];
    PIO_ERROR_LOG_PACKET p;
    USHORT DumpDataSize = 0;
    ULONG packetlen;

    RtlStringCchPrintfW((NTSTRSAFE_PWSTR)strStat, 12, L"0x%08x", stat);

    packetlen = (ULONG)(sizeof(IO_ERROR_LOG_PACKET) + DumpDataSize + (wcslen(strStat) + 1) * sizeof(WCHAR));
    if (msg)
        packetlen += (ULONG)((wcslen(msg) + 1) * sizeof(WCHAR));

    if (packetlen > ERROR_LOG_MAXIMUM_SIZE)
        return;
    p = (PIO_ERROR_LOG_PACKET)IoAllocateErrorLogEntry(pObj, (UCHAR)packetlen);
    if (!p)
        return;

    memset(p, 0, sizeof(IO_ERROR_LOG_PACKET));
    p->ErrorCode = code;
    p->FinalStatus = stat;


    p->DumpDataSize = DumpDataSize;
    //p->DumpData[0] = <whatever>;

    p->StringOffset = sizeof(IO_ERROR_LOG_PACKET) + p->DumpDataSize;

    if (msg) {
        p->NumberOfStrings = 2;
        //wcscpy((PWSTR)((PUCHAR)p + p->StringOffset), msg);
        wcscpy_s((PWSTR)((PUCHAR)p + p->StringOffset), (ULONG)(wcslen(msg) + 1), msg);	 // TODO: Fix this line and remove the line above

        //wcscpy((PWSTR)((PUCHAR)p + p->StringOffset + (wcslen(msg) + 1) * sizeof(WCHAR)), strStat);
        wcscpy_s((PWSTR)((PUCHAR)p + p->StringOffset + (wcslen(msg) + 1) * sizeof(WCHAR)), wcslen(strStat) + 1, strStat); // TODO: Fix this line and remove the line above
    }
    else {
        p->NumberOfStrings = 1;
        //wcscpy((PWSTR)((PUCHAR)p + p->StringOffset), strStat);
        wcscpy_s((PWSTR)((PUCHAR)p + p->StringOffset), wcslen(strStat) + 1, strStat);		// TODO: Fix this line and remove the line above
    };

    IoWriteErrorLogEntry(p);
}

#pragma warning(default: 4995)


#if !defined(EVENT_TRACING)
VOID
TraceEvents(
    IN ULONG   TraceEventsLevel,
    IN ULONG   TraceEventsFlag,
    IN PCCHAR  DebugMessage,
    ...
)
{
#if DBG
#define     TEMP_BUFFER_SIZE        512
    va_list    list;
    CHAR       debugMessageBuffer[TEMP_BUFFER_SIZE];
    NTSTATUS   status;

    va_start(list, DebugMessage);

    if (DebugMessage) {

        //
        // Using new safe string functions instead of _vsnprintf.
        // This function takes care of NULL terminating if the message
        // is longer than the buffer.
        //
        status = RtlStringCbVPrintfA(debugMessageBuffer,
            sizeof(debugMessageBuffer),
            DebugMessage,
            list);
        if (!NT_SUCCESS(status)) {

            DbgPrint(_DRIVER_NAME_": RtlStringCbVPrintfA failed 0x%x\n", status);
            return;
        }
        if (TraceEventsLevel <= TRACE_LEVEL_ERROR ||
            (TraceEventsLevel <= DebugLevel &&
                ((TraceEventsFlag & DebugFlag) == TraceEventsFlag))) {
            DbgPrint("%s%s", _DRIVER_NAME_, debugMessageBuffer);
        }
    }
    va_end(list);

    return;
#else
    UNREFERENCED_PARAMETER(TraceEventsLevel);
    UNREFERENCED_PARAMETER(TraceEventsFlag);
    UNREFERENCED_PARAMETER(DebugMessage);
#endif
}

#endif

