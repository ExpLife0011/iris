// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// hybrid model
#define KDEXT_64BIT
#define DBG_COMMAND_EXCEPTION            ((DWORD   )0x40010009L)
#include <wdbgexts.h>
#include <dbgeng.h>

#define EXIT_API     ExtRelease

// it used to be MAX_PATH (260) not anymore in Windows 10
#define CUSTOM_MAX		1040