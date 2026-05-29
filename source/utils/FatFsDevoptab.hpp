#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool FatFsDevoptab_Register(const char* deviceName, unsigned char driveNumber);
bool FatFsDevoptab_Unregister();

#ifdef __cplusplus
}
#endif
