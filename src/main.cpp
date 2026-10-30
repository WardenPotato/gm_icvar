
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <icvar.h>
#include "convar.h"

#include <Windows.h>

#include <cstdio>
#include <cstdarg>
#include <cstdint>

// ============================================================
// Global diagnostic log
// ============================================================

static FILE* g_DiagnosticLog = nullptr;

// Forward declaration
static void LuaPrint(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* message
);

// ============================================================
// Lua printing
// ============================================================

static void LuaPrint(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* message)
{
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "print");
    LUA->PushString(message);
    LUA->Call(1, 0);
    LUA->Pop();
}

// ============================================================
// Persistent logging
// ============================================================

static void InitDiagnosticLog()
{
    if (g_DiagnosticLog != nullptr)
        return;

    g_DiagnosticLog = std::fopen(
        "garrysmod/data/icvar_diagnostic.log",
        "a"
    );

    if (g_DiagnosticLog != nullptr)
    {
        std::fprintf(
            g_DiagnosticLog,
            "\n========== ICVAR DIAGNOSTIC SESSION ==========\n"
        );

        std::fflush(g_DiagnosticLog);
    }
}

static void LogDiagnostic(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* format,
    ...)
{
    char message[1024];

    va_list args;
    va_start(args, format);

    std::vsnprintf(
        message,
        sizeof(message),
        format,
        args
    );

    va_end(args);

    if (LUA != nullptr)
        LuaPrint(LUA, message);

    InitDiagnosticLog();

    if (g_DiagnosticLog != nullptr)
    {
        std::fprintf(
            g_DiagnosticLog,
            "%s\n",
            message
        );

        std::fflush(g_DiagnosticLog);
    }
}

// ============================================================
// Pointer logging
// ============================================================

static void LogPointer(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* pointer)
{
    LogDiagnostic(
        LUA,
        "[icvar] %s = %p",
        label,
        pointer
    );
}

// ============================================================
// Module identification
//
// Finds the module containing an address and calculates
// the address RVA relative to that module's loaded base.
//
// Does not require Psapi.lib.
// ============================================================

static void LogModuleForAddress(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* address)
{
    LogDiagnostic(
        LUA,
        "[module] Inspecting %s",
        label
    );

    if (address == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[module] %s: address is null",
            label
        );

        return;
    }

    HMODULE module = nullptr;

    const BOOL success = GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address),
        &module
    );

    if (!success || module == nullptr)
    {
        const DWORD error = GetLastError();

        LogDiagnostic(
            LUA,
            "[module] %s: GetModuleHandleExA failed, error=%lu",
            label,
            static_cast<unsigned long>(error)
        );

        return;
    }

    char path[MAX_PATH] = {};

    const DWORD pathLength = GetModuleFileNameA(
        module,
        path,
        MAX_PATH
    );

    if (pathLength == 0)
    {
        LogDiagnostic(
            LUA,
            "[module] %s: GetModuleFileNameA failed, error=%lu",
            label,
            static_cast<unsigned long>(GetLastError())
        );
    }

    const uintptr_t base =
        reinterpret_cast<uintptr_t>(module);

    const uintptr_t addr =
        reinterpret_cast<uintptr_t>(address);

    const uintptr_t rva = addr - base;

    LogDiagnostic(
        LUA,
        "[module] %s path: %s",
        label,
        pathLength != 0 ? path : "<unavailable>"
    );

    LogDiagnostic(
        LUA,
        "[module] %s base: %p",
        label,
        module
    );

    LogDiagnostic(
        LUA,
        "[module] %s address: %p",
        label,
        address
    );

    LogDiagnostic(
        LUA,
        "[module] %s RVA: 0x%llX",
        label,
        static_cast<unsigned long long>(rva)
    );
}

// ============================================================
// Vtable inspection
//
// Reads the object's vtable pointer and a bounded number
// of entries. Does not invoke virtual functions.
// ============================================================

static void DumpVtable(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int count)
{
    LogDiagnostic(
        LUA,
        "[diag] Beginning vtable inspection: %s",
        label
    );

    if (object == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[diag] ERROR: %s object is null",
            label
        );

        return;
    }

    void** vtable = *reinterpret_cast<void***>(
        const_cast<void*>(object)
    );

    LogDiagnostic(
        LUA,
        "[diag] %s object=%p vtable=%p",
        label,
        object,
        static_cast<void*>(vtable)
    );

    if (vtable == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[diag] ERROR: %s vtable is null",
            label
        );

        return;
    }

    for (int i = 0; i < count; ++i)
    {
        LogDiagnostic(
            LUA,
            "[diag] %s vtable[%02d] = %p",
            label,
            i,
            vtable[i]
        );
    }

    LogDiagnostic(
        LUA,
        "[diag] Finished vtable inspection: %s",
        label
    );
}

// ============================================================
// Inspect a vtable entry's containing module
// ============================================================

static void LogVtableEntryModule(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int index)
{
    if (object == nullptr)
        return;

    void** vtable = *reinterpret_cast<void***>(
        const_cast<void*>(object)
    );

    if (vtable == nullptr)
        return;

    void* entry = vtable[index];

    char entryLabel[128];

    std::snprintf(
        entryLabel,
        sizeof(entryLabel),
        "%s vtable[%d]",
        label,
        index
    );

    LogModuleForAddress(
        LUA,
        entryLabel,
        entry
    );
}

// ============================================================
// ICvar interface acquisition
// ============================================================

static ICvar* GetICvar(
    GarrysMod::Lua::ILuaBase* LUA)
{
    static SourceSDK::FactoryLoader loader("vstdlib");
    static ICvar* icvar = nullptr;
    static bool attempted = false;

    if (!attempted)
    {
        attempted = true;

        LogDiagnostic(
            LUA,
            "[icvar] Initializing VEngineCvar004"
        );

        if (!loader.IsValid())
        {
            LogDiagnostic(
                LUA,
                "[icvar] ERROR: vstdlib loader is invalid"
            );
        }
        else
        {
            LogDiagnostic(
                LUA,
                "[icvar] vstdlib loader is valid"
            );

            icvar = loader.GetInterface<ICvar>(
                "VEngineCvar004"
            );

            LogPointer(
                LUA,
                "ICvar interface",
                icvar
            );
        }
    }

    if (icvar == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[icvar] ERROR: Could not obtain VEngineCvar004"
        );

        LUA->ThrowError(
            "[icvar] Could not obtain VEngineCvar004"
        );

        return nullptr;
    }

    return icvar;
}

// ============================================================
// Find a ConVar
// ============================================================

static ConVar* GetConvar(
    const char* name,
    GarrysMod::Lua::ILuaBase* LUA)
{
    LogDiagnostic(
        LUA,
        "[icvar] GetConvar: obtaining ICvar"
    );

    ICvar* icvar = GetICvar(LUA);

    if (icvar == nullptr)
        return nullptr;

    LogPointer(
        LUA,
        "ICvar before FindVar",
        icvar
    );

    LogDiagnostic(
        LUA,
        "[icvar] Calling FindVar(\"%s\")",
        name
    );

    ConVar* convar = icvar->FindVar(name);

    LogPointer(
        LUA,
        "FindVar result",
        convar
    );

    if (convar == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[icvar] ERROR: FindVar returned null"
        );

        LUA->ThrowError(
            "[icvar] FindVar returned null"
        );

        return nullptr;
    }

    LogDiagnostic(
        LUA,
        "[icvar] FindVar completed successfully"
    );

    return convar;
}

// ============================================================
// Scan candidate interface versions
// ============================================================

LUA_FUNCTION(ListCvarInterfaces)
{
    LogDiagnostic(
        LUA,
        "[icvar] === Interface factory scan started ==="
    );

    SourceSDK::FactoryLoader loader("vstdlib");

    if (!loader.IsValid())
    {
        LogDiagnostic(
            LUA,
            "[icvar] ERROR: vstdlib or CreateInterface unavailable"
        );

        return 0;
    }

    LogDiagnostic(
        LUA,
        "[icvar] vstdlib loader is valid"
    );

    int found = 0;

    char name[64];

    for (int i = 1; i <= 99; ++i)
    {
        std::snprintf(
            name,
            sizeof(name),
            "VEngineCvar%03d",
            i
        );

        void* interfacePointer =
            loader.GetInterface<void>(name);

        if (interfacePointer != nullptr)
        {
            LogDiagnostic(
                LUA,
                "[icvar] FOUND %s at %p",
                name,
                interfacePointer
            );

            ++found;

            if (i == 4)
            {
                DumpVtable(
                    LUA,
                    "VEngineCvar004",
                    interfacePointer,
                    24
                );

                LogModuleForAddress(
                    LUA,
                    "ICvar object",
                    interfacePointer
                );

                LogModuleForAddress(
                    LUA,
                    "ICvar vtable",
                    *reinterpret_cast<void**>(interfacePointer)
                );
            }
        }
    }

    LogDiagnostic(
        LUA,
        "[icvar] === Scan finished: %d candidates ===",
        found
    );

    return 0;
}

// ============================================================
// Inspect primary ConVar vtable
// ============================================================

LUA_FUNCTION(InspectConVar)
{
    LUA->CheckType(
        1,
        GarrysMod::Lua::Type::STRING
    );

    const char* name = LUA->GetString(1);

    LogDiagnostic(
        LUA,
        "[icvar] === InspectConVar(\"%s\") ===",
        name
    );

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    DumpVtable(
        LUA,
        "ConVar primary",
        convar,
        20
    );

    LogModuleForAddress(
        LUA,
        "ConVar object",
        convar
    );

    LogModuleForAddress(
        LUA,
        "ConVar primary vtable",
        *reinterpret_cast<void**>(convar)
    );

    LogDiagnostic(
        LUA,
        "[icvar] Primary inspection completed"
    );

    return 0;
}

// ============================================================
// Diagnose ConVar and IConVar layout
//
// No virtual methods are invoked here.
// This is intentional: previous tests stopped at IsFlagSet.
// ============================================================

LUA_FUNCTION(DiagnoseConVar)
{
    LUA->CheckType(
        1,
        GarrysMod::Lua::Type::STRING
    );

    const char* name = LUA->GetString(1);

    LogDiagnostic(
        LUA,
        "[diag] Starting pointer and module diagnosis"
    );

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    LogDiagnostic(
        LUA,
        "[diag] Primary ConVar pointer obtained"
    );

    LogPointer(
        LUA,
        "ConVar primary object",
        convar
    );

    void** primaryVtable =
        *reinterpret_cast<void***>(convar);

    LogPointer(
        LUA,
        "ConVar primary vtable",
        primaryVtable
    );

    IConVar* iconvar =
        static_cast<IConVar*>(convar);

    LogDiagnostic(
        LUA,
        "[diag] static_cast<IConVar*> completed"
    );

    LogPointer(
        LUA,
        "IConVar secondary object",
        iconvar
    );

    const uintptr_t primaryAddress =
        reinterpret_cast<uintptr_t>(convar);

    const uintptr_t secondaryAddress =
        reinterpret_cast<uintptr_t>(iconvar);

    LogDiagnostic(
        LUA,
        "[diag] Secondary base adjustment: 0x%llX",
        static_cast<unsigned long long>(
            secondaryAddress - primaryAddress
        )
    );

    void** secondaryVtable =
        *reinterpret_cast<void***>(iconvar);

    LogPointer(
        LUA,
        "IConVar secondary vtable",
        secondaryVtable
    );

    LogDiagnostic(
        LUA,
        "[diag] Dumping primary vtable"
    );

    DumpVtable(
        LUA,
        "ConVar primary",
        convar,
        20
    );

    LogDiagnostic(
        LUA,
        "[diag] Dumping secondary vtable"
    );

    DumpVtable(
        LUA,
        "IConVar secondary",
        iconvar,
        5
    );

    // Object and vtable module information.
    LogModuleForAddress(
        LUA,
        "ConVar object",
        convar
    );

    LogModuleForAddress(
        LUA,
        "ConVar primary vtable",
        primaryVtable
    );

    LogModuleForAddress(
        LUA,
        "IConVar object",
        iconvar
    );

    LogModuleForAddress(
        LUA,
        "IConVar secondary vtable",
        secondaryVtable
    );

    // Primary vtable entries relevant to the ConVar interface.
    LogVtableEntryModule(
        LUA, "ConVar primary", convar, 18
    );

    LogVtableEntryModule(
        LUA, "ConVar primary", convar, 19
    );

    // Every entry in the secondary IConVar vtable.
    for (int i = 0; i < 5; ++i)
    {
        LogVtableEntryModule(
            LUA,
            "IConVar secondary",
            iconvar,
            i
        );
    }

    // ICvar interface module information.
    ICvar* icvar = GetICvar(LUA);

    if (icvar != nullptr)
    {
        void** icvarVtable =
            *reinterpret_cast<void***>(icvar);

        LogModuleForAddress(
            LUA,
            "ICvar object",
            icvar
        );

        LogModuleForAddress(
            LUA,
            "ICvar vtable",
            icvarVtable
        );

        LogVtableEntryModule(
            LUA,
            "ICvar",
            icvar,
            12
        );

        LogVtableEntryModule(
            LUA,
            "ICvar",
            icvar,
            13
        );
    }

    LogDiagnostic(
        LUA,
        "[diag] Pointer and module diagnosis completed"
    );

    return 0;
}

// ============================================================
// Original SetValue function
//
// Leave this registered for now, but do not use it during
// the current ABI investigation.
// ============================================================

LUA_FUNCTION(SetValue)
{
    LUA->CheckType(
        1,
        GarrysMod::Lua::Type::STRING
    );

    const char* name = LUA->GetString(1);

    const int type = LUA->GetType(2);

    LogDiagnostic(
        LUA,
        "[icvar] === SetValue(\"%s\") started ===",
        name
    );

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    if (type == GarrysMod::Lua::Type::NUMBER)
    {
        const float value =
            static_cast<float>(LUA->GetNumber(2));

        LogDiagnostic(
            LUA,
            "[icvar] Calling ConVar::SetValue(float): %.9g",
            static_cast<double>(value)
        );

        convar->SetValue(value);

        LogDiagnostic(
            LUA,
            "[icvar] ConVar::SetValue(float) returned"
        );
    }
    else if (type == GarrysMod::Lua::Type::STRING)
    {
        const char* value = LUA->GetString(2);

        LogDiagnostic(
            LUA,
            "[icvar] Calling ConVar::SetValue(string)"
        );

        convar->SetValue(value);

        LogDiagnostic(
            LUA,
            "[icvar] ConVar::SetValue(string) returned"
        );
    }
    else
    {
        LUA->ArgError(
            2,
            "Expected a number or string"
        );

        return 0;
    }

    LogDiagnostic(
        LUA,
        "[icvar] === SetValue completed ==="
    );

    return 0;
}

// ============================================================
// Module entry point
// ============================================================

GMOD_MODULE_OPEN()
{
    InitDiagnosticLog();

    LogDiagnostic(
        LUA,
        "[icvar] Module loaded"
    );

    LUA->PushSpecial(
        GarrysMod::Lua::SPECIAL_GLOB
    );

    LUA->CreateTable();

    LUA->PushCFunction(SetValue);
    LUA->SetField(-2, "SetValue");

    LUA->PushCFunction(ListCvarInterfaces);
    LUA->SetField(-2, "ListCvarInterfaces");

    LUA->PushCFunction(InspectConVar);
    LUA->SetField(-2, "InspectConVar");

    LUA->PushCFunction(DiagnoseConVar);
    LUA->SetField(-2, "DiagnoseConVar");

    LUA->SetField(-2, "icvar");

    LUA->Pop();

    return 0;
}

// ============================================================
// Module close
// ============================================================

GMOD_MODULE_CLOSE()
{
    LogDiagnostic(
        LUA,
        "[icvar] Module closing"
    );

    LUA->PushNil();

    LUA->SetField(
        GarrysMod::Lua::SPECIAL_GLOB,
        "icvar"
    );

    if (g_DiagnosticLog != nullptr)
    {
        std::fflush(g_DiagnosticLog);
        std::fclose(g_DiagnosticLog);
        g_DiagnosticLog = nullptr;
    }

    return 0;
}
