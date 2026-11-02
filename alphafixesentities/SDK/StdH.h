// Precompiled-header stand-in that entity sources include first, as in the
// later SDK.
#pragma once

#include <Engine/Engine.h>
#include <Entities/Common.h>

// Classes in this DLL export their tables; the alpha's own are imported.
#define DECL_DLL __declspec(dllexport)

#ifndef PlaySound
#define PlaySound PlaySoundA
#endif
