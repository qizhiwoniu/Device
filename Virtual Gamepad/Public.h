/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
// 定义接口Guid，以便应用程序可以找到设备并与其对话。

DEFINE_GUID (GUID_DEVINTERFACE_VirtualGamepad,
    0x945b89cd,0x5b93,0x4cd6,0xa4,0xa0,0x53,0x4c,0x79,0x62,0x7a,0xe9);
// {945b89cd-5b93-4cd6-a4a0-534c79627ae9}

// Device Strings
//设备字符串
#define VENDOR_STR_ID		L"Qizhiwoniu"
#define PRODUCT_STR_ID		L"Virtual Gamepad Bus"

#define  Joystick_HARDWARE_ID     L"ROOT\SYSTEM\0001"
#define  Joystick_RAW_DEVICE_ID	  L"ROOT\SYSTEM\0001"



