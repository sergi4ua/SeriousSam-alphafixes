// Shared by the DLL's own sources (not by entity classes).
#pragma once

#include <windows.h>
#include <stdio.h>
#include <string.h>

DWORD ModuleTimestamp(HMODULE hModule);

// Finds the alpha's CEnemyBase code; false (with the reason) if this isn't the
// Entities.dll build the SDK was made from.
bool InitEnemyBase(char *strError, size_t ctError);
