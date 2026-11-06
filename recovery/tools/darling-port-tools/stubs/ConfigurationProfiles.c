// Stand-in for ConfigurationProfiles.framework, which BaseSystem apps weak-link but BaseSystem does not ship
// (Language Chooser calls CPIsDeviceRegisteredWithDEPProgram without checking it is there). Darling side, not Apple
// code: the device is not enrolled in DEP and has no activation record.
//   clang -arch x86_64 -mmacosx-version-min=14.0 -dynamiclib -install_name \
//     /System/Library/PrivateFrameworks/ConfigurationProfiles.framework/Versions/A/ConfigurationProfiles \
//     -o ConfigurationProfiles ConfigurationProfiles.c
#include <stdbool.h>
#include <stddef.h>

bool CPIsDeviceRegisteredWithDEPProgram(void *error)
{
	(void)error;
	return false;
}

void *CPGetActivationRecord(void *error)
{
	(void)error;
	return NULL;
}
