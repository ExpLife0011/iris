// iris.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

WINDBG_EXTENSION_APIS64 ExtensionApis;

// Global variables initialized by query - required COM interfaces
IDebugControl* pDebugControl = NULL;
IDebugClient4* pDebugClient = NULL;
IDebugSymbols* pSymbols = NULL;
IDebugSymbols3* pSymbols3 = NULL;
IDebugDataSpaces3* pDataSpaces = NULL;
BOOL bInitialized = FALSE;

// Queries for all debugger interfaces.
VOID ExtRelease()
{
	if (pDebugClient) pDebugClient->Release();
	if (pDebugControl) pDebugControl->Release();
	if (pSymbols) pSymbols->Release();
	if (pSymbols3) pSymbols3->Release();
	if (pDataSpaces) pDataSpaces->Release();
}

// INIT_API() encapsulates a call to ExtQuery() and EXIT_API encapsulates ExtRelease()
extern "C" HRESULT ExtQuery(PDEBUG_CLIENT4 Client)
{
	pDebugClient = Client;
	HRESULT Status;

	if ((Status = Client->QueryInterface(__uuidof(IDebugControl), (void **)&pDebugControl)) != S_OK)
	{
		goto Fail;
	}
	if ((Status = Client->QueryInterface(__uuidof(IDebugSymbols2), (void **)&pSymbols3)) != S_OK)
	{
		goto Fail;
	}

	return S_OK;

Fail:
	ExtRelease();
	return Status;
}

// All required engine interfaces are queried from the DEBUG_CLIENT using the INIT_API() macro
#define INIT_API()                             \
    HRESULT Status;                            \
    if ((Status = ExtQuery(Client)) != S_OK) return Status;

#define EXT_RELEASE(Unk) \
    ((Unk) != NULL ? ((Unk)->Release(), (Unk) = NULL) : NULL)

const char * checkDynamicBase(ULONG ModuleIndex)
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	if (ModuleHeaders.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
		return "Yes";

	return "No";
}

const char * checkASLR(ULONG ModuleIndex)
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	if (!(ModuleHeaders.FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) && (ModuleHeaders.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
		return "Yes";
		
	return "No";
}

const char * checkDEP(ULONG ModuleIndex) 
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	if (ModuleHeaders.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT) 
		return "Yes";
		
	return "No";
}

const char * checkSEH(ULONG ModuleIndex) {
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	if (ModuleHeaders.OptionalHeader.DllCharacteristics == 0)
		return "No";

	if (ModuleHeaders.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NO_SEH)
		return "No";

	return "Yes";
}

const char * checkSafeSEH(ULONG ModuleIndex) {
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	ULONG BytesRead = 0;
	ULONG64 LoadConfigVA = 0;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);
#ifndef _WIN64
	IMAGE_LOAD_CONFIG_DIRECTORY32 ModuleConfigDir;
#else
	IMAGE_LOAD_CONFIG_DIRECTORY64 ModuleConfigDir;
#endif
	LoadConfigVA = ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
	pDataSpaces->ReadVirtual(ModuleBase + LoadConfigVA, (PVOID)&ModuleConfigDir, sizeof(ModuleConfigDir), &BytesRead);

	if (ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size == 0) // Error: assume NO
		return "No";

	// a load config under 112 bytes implies the absence of the SafeSEH fields
	if (ModuleConfigDir.Size < 112)
		return "No";

	if (ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress == 0)
		return "No";
	
	// 
	//dprintf("\n%p - %p\n", ModuleBase, LoadConfigVA);
	if (ModuleConfigDir.SEHandlerCount == 0 && ModuleConfigDir.SEHandlerTable == 0)
		return "No";

	return "Yes";
}

const char * checkCFG(ULONG ModuleIndex)
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	if (ModuleHeaders.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF)
		return "Yes";
		
	return "No";
}

// not realiable as is
const char * checkRFG(ULONG ModuleIndex)
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
#ifndef _WIN64
	IMAGE_LOAD_CONFIG_DIRECTORY32 ModuleConfigDir;
#else
	IMAGE_LOAD_CONFIG_DIRECTORY64 ModuleConfigDir;
#endif
	ULONG BytesRead = 0;
	ULONG64 ModuleBase = 0, LoadConfigVA = 0;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	LoadConfigVA = ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
	pDataSpaces->ReadVirtual(ModuleBase + LoadConfigVA, (PVOID)&ModuleConfigDir, sizeof(ModuleConfigDir), &BytesRead);

	if (ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size == 0) // no load config assume NO
		return "No";

	// a load config under 148 bytes implies the absence of the GuardFlags field
	if (ModuleConfigDir.Size < 148)
		return "No";

	if (((ModuleConfigDir.GuardFlags & IMAGE_GUARD_RF_INSTRUMENTED) && (ModuleConfigDir.GuardFlags & IMAGE_GUARD_RF_ENABLE || ModuleConfigDir.GuardFlags & IMAGE_GUARD_RF_STRICT)) == 0)
		return "No";
	
	return "Yes";
}

const char * checkGS(ULONG ModuleIndex)
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
	IMAGE_LOAD_CONFIG_DIRECTORY32 ModuleConfigDir;
	ULONG BytesRead = 0;
	ULONG64 ModuleBase = 0, LoadConfigVA = 0;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	LoadConfigVA = ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
	pDataSpaces->ReadVirtual(ModuleBase + LoadConfigVA, (PVOID)&ModuleConfigDir, sizeof(ModuleConfigDir), &BytesRead);

	if (ModuleHeaders.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size == 0) // assume No
		return "No";

	// a load config under 96 bytes implies the absence of the SecurityCookie field
	if (ModuleConfigDir.Size < 96)
		return "No";

	return "Yes";
}

const char * checkAppContainer(ULONG ModuleIndex)
{
	IMAGE_NT_HEADERS64 ModuleHeaders;
	ULONG64 ModuleBase;
	pSymbols3->GetModuleByIndex(ModuleIndex, &ModuleBase);
	pDataSpaces->ReadImageNtHeaders(ModuleBase, &ModuleHeaders);

	if (ModuleHeaders.OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_APPCONTAINER)
		return "Yes";
		
	return "No";
}

HRESULT CALLBACK modules(PDEBUG_CLIENT4 Client, PCSTR args) {
	INIT_API();

	ULONG index = 0, NrModulesLoaded = 0, NrModulesUnloaded = 0, CurrentModuleNameSize = 0, CurrentImageNameSize = 0, CurrentLoadedImageNameSize = 0;
	ULONG64 CurrentModuleBase = 0;
	IMAGE_NT_HEADERS64 CurrentModuleHeaders;
	CHAR CurrentModuleName[CUSTOM_MAX] = { 0 };
	CHAR CurrentImageName[CUSTOM_MAX] = { 0 };
	CHAR CurrentLoadedImageName[CUSTOM_MAX] = { 0 };

	pSymbols3->GetNumberModules(&NrModulesLoaded, &NrModulesUnloaded);

#ifndef _WIN64
	dprintf("Base     Size     DynamicBase ASLR DEP SEH SafeSEH CFG  RFG GS  AppContainer Module \n");
#else
	dprintf("Base             Size     DynamicBase ASLR DEP SEH SafeSEH CFG  RFG GS  AppContainer Module \n");
#endif

	while (pSymbols3->GetModuleByIndex(index, &CurrentModuleBase) == S_OK) 
	{
		// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/dbgeng/nf-dbgeng-idebugsymbols3-getmodulenames
		pSymbols3->GetModuleNames(index, CurrentModuleBase, CurrentImageName, sizeof(CurrentImageName) - 1, &CurrentImageNameSize, CurrentModuleName, sizeof(CurrentModuleName) - 1,
			&CurrentModuleNameSize, CurrentLoadedImageName, sizeof(CurrentLoadedImageName) - 1, &CurrentLoadedImageNameSize);

		// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/dbgeng/nf-dbgeng-idebugdataspaces4-readimagentheaders
		pDataSpaces->ReadImageNtHeaders(CurrentModuleBase, &CurrentModuleHeaders);

		// If CurrentModuleName is 0 move on (module is not loaded)
		if (strlen(CurrentModuleName) == 0) 
		{
			index++;
			continue;
		}
		else 
		{
			dprintf("%p %08x %-11s %-4s %-3s %-3s %-7s %-4s %-3s %-3s %-12s %s\n",
				CurrentModuleBase,
				CurrentModuleHeaders.OptionalHeader.SizeOfImage,
				checkDynamicBase(index),
				checkASLR(index),
				checkDEP(index),
				checkSEH(index),
				checkSafeSEH(index),
				checkCFG(index),
				checkRFG(index),
				checkGS(index),
				checkAppContainer(index),
				CurrentImageName);

			index++;
		}

		CurrentModuleBase = 0;
		memset(&CurrentModuleHeaders, 0, sizeof(CurrentModuleHeaders));
		memset(CurrentModuleName, 0, sizeof(CurrentModuleName));
		memset(CurrentImageName, 0, sizeof(CurrentImageName));
		memset(CurrentLoadedImageName, 0, sizeof(CurrentLoadedImageName));
	}

	ExtRelease();

	return S_OK;
}

// The entry point for the extension
extern "C" HRESULT CALLBACK DebugExtensionInitialize(PULONG Version, PULONG Flags) {
	HRESULT hRes = E_FAIL;

	if (bInitialized)
	{
		return S_OK;
	}

	// Initialize the version information
	*Version = DEBUG_EXTENSION_VERSION(0, 2);
	*Flags = 0;

	// Initialize required COM interface pointers
	if (FAILED(hRes = DebugCreate(__uuidof(IDebugClient), (void**)&pDebugClient)))
	{
		return hRes;
	}

	if (FAILED(hRes = pDebugClient->QueryInterface(__uuidof(IDebugControl), (void**)&pDebugControl)))
	{
		ExtRelease();
		return hRes;
	}

	// Initialize WinDbg extension data
	ExtensionApis.nSize = sizeof(ExtensionApis);
	hRes = pDebugControl->GetWindbgExtensionApis64(&ExtensionApis);

	if (FAILED((hRes = pDebugClient->QueryInterface(__uuidof(IDebugDataSpaces), (void**)&pDataSpaces))))
	{
		dprintf("[-] Failed to get required COM interface\n");
		ExtRelease();
		return hRes;
	}

	if (FAILED(hRes = pDebugClient->QueryInterface(__uuidof(IDebugSymbols), (void**)&pSymbols)))
	{
		dprintf("[-] Failed to get required COM interface\n");
		ExtRelease();
		return hRes;
	}

	if (FAILED(hRes = pDebugClient->QueryInterface(__uuidof(IDebugSymbols3), (void**)&pSymbols3)))
	{
		dprintf("[-] Failed to get required COM interface\n");
		ExtRelease();
		return hRes;
	}

	dprintf("[+] Iris WinDbg Extension Loaded\n");

	bInitialized = TRUE;
	hRes = S_OK;

	return hRes;
}

extern "C" void CALLBACK DebugExtensionUninitialize(void)
{
	ExtRelease();
	return;
}

HRESULT CALLBACK help(PDEBUG_CLIENT Client, PCSTR Args)
{
	dprintf("\nIRIS WinDbg Extension (rui@deniable.org). Available commands:\n"
		"\thelp                  = Shows this help\n"
		"\tmodules               = Display exploit mitigations for all loaded modules.\n");

	return S_OK;
}

