#pragma once
#include <cstdint>
using u32 = uint32_t;
using u64 = uint64_t;
using Result = uint32_t;
#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
enum HidNpadIdType { HidNpadIdType_No1 = 0, HidNpadIdType_Handheld = 32 };
enum HidNpadStyleTag { HidNpadStyleTag_NpadFullKey = 1, HidNpadStyleTag_NpadHandheld = 2,
    HidNpadStyleTag_NpadJoyDual = 4, HidNpadStyleTag_NpadJoyLeft = 8, HidNpadStyleTag_NpadJoyRight = 16 };
enum HidNpadAttribute { HidNpadAttribute_IsConnected = 1,
    HidNpadAttribute_IsLeftConnected = 4, HidNpadAttribute_IsRightConnected = 16 };
enum HidSixAxisSensorAttribute { HidSixAxisSensorAttribute_IsConnected = 1 };
struct HidSixAxisSensorHandle { int value = 0; };
struct HidVector { float x = 0, y = 0, z = 0; };
struct HidSixAxisSensorState {
    u64 sampling_number = 0;
    HidVector acceleration{}, angular_velocity{};
    u32 attributes = 0;
};
struct PadState { HidNpadIdType id{}; bool active_handheld = false; u32 style_set = 0; u64 attributes = 0; };
void padInitialize(PadState*, HidNpadIdType);
void padUpdate(PadState*);
u32 padGetStyleSet(PadState*);
u64 padGetAttributes(PadState*);
Result hidGetSixAxisSensorHandles(HidSixAxisSensorHandle*, int, HidNpadIdType, HidNpadStyleTag);
Result hidStartSixAxisSensor(HidSixAxisSensorHandle);
Result hidStopSixAxisSensor(HidSixAxisSensorHandle);
int hidGetSixAxisSensorStates(HidSixAxisSensorHandle, HidSixAxisSensorState*, int);
enum HidGyroscopeZeroDriftMode { HidGyroscopeZeroDriftMode_Loose = 0,
    HidGyroscopeZeroDriftMode_Standard = 1, HidGyroscopeZeroDriftMode_Tight = 2 };
Result hidSetGyroscopeZeroDriftMode(HidSixAxisSensorHandle, HidGyroscopeZeroDriftMode);
