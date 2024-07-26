
#include <joystick.h>

#if defined(EVENT_TRACING)
#include "usb.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, JoystickConfigContReaderForInterruptEndPoint)
#endif

PCHAR
DbgDevicePowerString(
    IN WDF_POWER_DEVICE_STATE Type
)

{
    switch (Type) {
        case WdfPowerDeviceInvalid:
            return "WdfPowerDeviceInvalid";
        case WdfPowerDeviceD0:
            return "WdfPowerDeviceD0";
        case PowerDeviceD1:
            return "WdfPowerDeviceD1";
        case WdfPowerDeviceD2:
            return "WdfPowerDeviceD2";
        case WdfPowerDeviceD3:
            return "WdfPowerDeviceD3";
        case WdfPowerDeviceD3Final:
            return "WdfPowerDeviceD3Final";
        case WdfPowerDevicePrepareForHibernation:
            return "WdfPowerDevicePrepareForHibernation";
        case WdfPowerDeviceMaximum:
            return "PowerDeviceMaximum";
        default:
            return "UnKnown Device Power State";
    }
}

