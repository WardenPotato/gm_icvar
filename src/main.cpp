
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <icvar.h>
#include "convar.h"

#include <Windows.h>

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstddef>

// ============================================================
// Global diagnostic log
// ============================================================

static FILE* g_DiagnosticLog = nullptr;

// ============================================================
// Forward declarations
// ============================================================

static void LuaPrint(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* message
);

static bool ReadSelfMemory(
    const void* address,
    void* output,
    size_t size
);

static ConVar* FindVarRaw(
    ICvar* icvar,
    const char* name
);

// ============================================================
// Lua printing
// ============================================================

static void LuaPrint(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* message)
{
    LUA->PushSpecial(
        GarrysMod::Lua::SPECIAL_GLOB
    );

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
        "%s = %p",
        label,
        pointer
    );
}

// ============================================================
// Safe memory reading
//
// ReadProcessMemory is used so that a bad diagnostic pointer
// does not immediately cause a direct access violation.
// ============================================================

static bool ReadSelfMemory(
    const void* address,
    void* output,
    size_t size)
{
    if (address == nullptr || output == nullptr || size == 0)
        return false;

    SIZE_T bytesRead = 0;

    const BOOL success = ReadProcessMemory(
        GetCurrentProcess(),
        address,
        output,
        size,
        &bytesRead
    );

    return success && bytesRead == size;
}

// ============================================================
// Read and log a string from a memory address
// ============================================================

static void LogMemoryString(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    uintptr_t address)
{
    if (address == 0)
    {
        LogDiagnostic(
            LUA,
            "[memory] %s: nullptr",
            label
        );

        return;
    }

    char text[128] = {};
    char hex[64] = {};

    bool readable = true;
    bool terminated = false;

    for (size_t i = 0; i < sizeof(text) - 1; ++i)
    {
        char character = '\0';

        if (!ReadSelfMemory(
            reinterpret_cast<const void*>(address + i),
            &character,
            sizeof(character)))
        {
            readable = false;
            break;
        }

        text[i] = character;

        if (character == '\0')
        {
            terminated = true;
            break;
        }
    }

    for (size_t i = 0; i < 16; ++i)
    {
        unsigned char byte = 0;

        if (!ReadSelfMemory(
            reinterpret_cast<const void*>(address + i),
            &byte,
            sizeof(byte)))
        {
            std::snprintf(
                hex + i * 3,
                sizeof(hex) - i * 3,
                "?? "
            );

            continue;
        }

        std::snprintf(
            hex + i * 3,
            sizeof(hex) - i * 3,
            "%02X ",
            static_cast<unsigned int>(byte)
        );
    }

    LogDiagnostic(
        LUA,
        "[memory] %s pointer=%p",
        label,
        reinterpret_cast<const void*>(address)
    );

    LogDiagnostic(
        LUA,
        "[memory] %s text='%s' readable=%s terminated=%s",
        label,
        text,
        readable ? "yes" : "no",
        terminated ? "yes" : "no"
    );

    LogDiagnostic(
        LUA,
        "[memory] %s bytes: %s",
        label,
        hex
    );
}

// ============================================================
// Inspect ConVar name storage
//
// Based on the engine instructions examined so far:
//
// ConVar + 0x38 = self-reference
// ConVar + 0x18 = name pointer
// ConVar + 0x28 = flags
// ============================================================

static void InspectNameFields(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object)
{
    if (object == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[memory] %s: object is null",
            label
        );

        return;
    }

    const uintptr_t address =
        reinterpret_cast<uintptr_t>(object);

    uintptr_t owner = 0;

    if (!ReadSelfMemory(
        reinterpret_cast<const void*>(address + 0x38),
        &owner,
        sizeof(owner)))
    {
        LogDiagnostic(
            LUA,
            "[memory] %s: unable to read owner at +0x38",
            label
        );

        return;
    }

    LogPointer(
        LUA,
        "[memory] Name owner",
        reinterpret_cast<void*>(owner)
    );

    if (owner == 0)
        return;

    uintptr_t namePointer = 0;

    if (!ReadSelfMemory(
        reinterpret_cast<const void*>(owner + 0x18),
        &namePointer,
        sizeof(namePointer)))
    {
        LogDiagnostic(
            LUA,
            "[memory] %s: unable to read name pointer",
            label
        );

        return;
    }

    LogMemoryString(
        LUA,
        label,
        namePointer
    );
}

// ============================================================
// Module identification
//
// Finds the loaded module containing an address and calculates
// its RVA relative to the module's actual loaded base.
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
        LogDiagnostic(
            LUA,
            "[module] %s: module lookup failed, error=%lu",
            label,
            static_cast<unsigned long>(GetLastError())
        );

        return;
    }

    char path[MAX_PATH] = {};

    const DWORD pathLength = GetModuleFileNameA(
        module,
        path,
        MAX_PATH
    );

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
// ============================================================

static void DumpVtable(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int count)
{
    LogDiagnostic(
        LUA,
        "[vtable] Beginning inspection: %s",
        label
    );

    if (object == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[vtable] ERROR: object is null"
        );

        return;
    }

    void** vtable = *reinterpret_cast<void***>(
        const_cast<void*>(object)
    );

    LogDiagnostic(
        LUA,
        "[vtable] %s object=%p vtable=%p",
        label,
        object,
        static_cast<void*>(vtable)
    );

    if (vtable == nullptr)
        return;

    for (int i = 0; i < count; ++i)
    {
        LogDiagnostic(
            LUA,
            "[vtable] %s[%02d] = %p",
            label,
            i,
            vtable[i]
        );
    }

    LogDiagnostic(
        LUA,
        "[vtable] Finished inspection: %s",
        label
    );
}

// ============================================================
// Vtable entry module inspection
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
// Inspect the modules owning an object's vtables and entries
// ============================================================

static void InspectObjectModules(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int primaryCount,
    bool inspectSecondary)
{
    if (object == nullptr)
        return;

    LogDiagnostic(
        LUA,
        "[module] ===== Object inspection: %s =====",
        label
    );

    void** primaryVtable = *reinterpret_cast<void***>(
        const_cast<void*>(object)
    );

    LogModuleForAddress(
        LUA,
        "Object",
        object
    );

    LogModuleForAddress(
        LUA,
        "Primary vtable",
        primaryVtable
    );

    for (int i = 0; i < primaryCount; ++i)
    {
        char entryLabel[128];

        std::snprintf(
            entryLabel,
            sizeof(entryLabel),
            "%s primary vtable[%d]",
            label,
            i
        );

        LogModuleForAddress(
            LUA,
            entryLabel,
            primaryVtable[i]
        );
    }

    if (inspectSecondary)
    {
        const ConVar* constVar =
            static_cast<const ConVar*>(object);

        const IConVar* secondary =
            static_cast<const IConVar*>(constVar);

        void** secondaryVtable =
            *reinterpret_cast<void***>(
                const_cast<IConVar*>(secondary)
            );

        LogModuleForAddress(
            LUA,
            "Secondary IConVar object",
            secondary
        );

        LogModuleForAddress(
            LUA,
            "Secondary IConVar vtable",
            secondaryVtable
        );

        for (int i = 0; i < 5; ++i)
        {
            char entryLabel[128];

            std::snprintf(
                entryLabel,
                sizeof(entryLabel),
                "%s secondary vtable[%d]",
                label,
                i
            );

            LogModuleForAddress(
                LUA,
                entryLabel,
                secondaryVtable[i]
            );
        }
    }
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
                "[icvar] ICvar interface",
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
// Raw ICvar lookup
//
// Uses the observed VEngineCvar004 vtable layout:
//
// 10 = FindCommandBase
// 11 = FindCommandBase const
// 12 = FindVar
// 13 = FindVar const
//
// Slot 12 has been tested against both r_3dsky and r_spewleaf.
// It returned the correct distinct objects.
// ============================================================

using RawLookupFn = void* (*)(
    void* self,
    const char* name
);

static ConVar* FindVarRaw(
    ICvar* icvar,
    const char* name)
{
    if (icvar == nullptr || name == nullptr)
        return nullptr;

    void** vtable =
        *reinterpret_cast<void***>(icvar);

    if (vtable == nullptr)
        return nullptr;

    RawLookupFn findVar =
        reinterpret_cast<RawLookupFn>(
            vtable[12]
        );

    if (findVar == nullptr)
        return nullptr;

    return reinterpret_cast<ConVar*>(
        findVar(icvar, name)
    );
}

// ============================================================
// Find a ConVar
//
// IMPORTANT: Uses FindVarRaw, not typed icvar->FindVar().
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
        "[icvar] ICvar before raw FindVar",
        icvar
    );

    LogDiagnostic(
        LUA,
        "[icvar] Calling raw FindVar slot 12 for '%s'",
        name
    );

    ConVar* convar = FindVarRaw(
        icvar,
        name
    );

    LogPointer(
        LUA,
        "[icvar] Raw FindVar result",
        convar
    );

    if (convar == nullptr)
    {
        LogDiagnostic(
            LUA,
            "[icvar] ERROR: Raw FindVar returned null"
        );

        LUA->ThrowError(
            "[icvar] Raw FindVar returned null"
        );

        return nullptr;
    }

    LogDiagnostic(
        LUA,
        "[icvar] Raw FindVar completed successfully"
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
                    *reinterpret_cast<void**>(
                        interfacePointer
                    )
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
        "[icvar] === InspectConVar('%s') ===",
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
// Diagnose ConVar
//
// Does not call any ConVar virtual methods.
// Uses raw FindVar and inspects the object and vtables.
// ============================================================

LUA_FUNCTION(DiagnoseConVar)
{
    LUA->CheckType(
        1,
        GarrysMod::Lua::Type::STRING
    );

    char name[256] = {};

    const char* luaName = LUA->GetString(1);

    std::snprintf(
        name,
        sizeof(name),
        "%s",
        luaName != nullptr ? luaName : ""
    );

    LogDiagnostic(
        LUA,
        "[diag] Starting ConVar diagnosis: '%s'",
        name
    );

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    LogPointer(
        LUA,
        "[diag] Primary ConVar",
        convar
    );

    IConVar* iconvar =
        static_cast<IConVar*>(convar);

    LogPointer(
        LUA,
        "[diag] Secondary IConVar",
        iconvar
    );

    const uintptr_t primary =
        reinterpret_cast<uintptr_t>(convar);

    const uintptr_t secondary =
        reinterpret_cast<uintptr_t>(iconvar);

    LogDiagnostic(
        LUA,
        "[diag] Secondary base adjustment: 0x%llX",
        static_cast<unsigned long long>(
            secondary - primary
        )
    );

    InspectNameFields(
        LUA,
        "[diag] ConVar name",
        convar
    );

    uint32_t flags = 0;

    if (ReadSelfMemory(
        reinterpret_cast<const void*>(primary + 0x28),
        &flags,
        sizeof(flags)))
    {
        LogDiagnostic(
            LUA,
            "[diag] Raw flags: 0x%08X",
            static_cast<unsigned int>(flags)
        );
    }
    else
    {
        LogDiagnostic(
            LUA,
            "[diag] Could not read raw flags"
        );
    }

    DumpVtable(
        LUA,
        "ConVar primary",
        convar,
        20
    );

    DumpVtable(
        LUA,
        "IConVar secondary",
        iconvar,
        5
    );

    InspectObjectModules(
        LUA,
        "ConVar",
        convar,
        20,
        true
    );

    LogDiagnostic(
        LUA,
        "[diag] ConVar diagnosis completed"
    );

    return 0;
}

// ============================================================
// Raw lookup diagnosis
//
// Compares typed calls with raw vtable calls.
// Inspects both returned objects and all relevant vtable
// implementation modules.
// ============================================================

LUA_FUNCTION(DiagnoseLookup)
{
    ICvar* icvar = GetICvar(LUA);

    if (icvar == nullptr)
        return 0;

    void** icvarVtable =
        *reinterpret_cast<void***>(icvar);

    LogPointer(
        LUA,
        "[raw] ICvar object",
        icvar
    );

    LogPointer(
        LUA,
        "[raw] ICvar vtable",
        icvarVtable
    );

    if (icvarVtable == nullptr)
        return 0;

    for (int i = 10; i <= 13; ++i)
    {
        LogDiagnostic(
            LUA,
            "[raw] ICvar vtable[%d] = %p",
            i,
            icvarVtable[i]
        );

        char label[64];

        std::snprintf(
            label,
            sizeof(label),
            "ICvar vtable[%d]",
            i
        );

        LogModuleForAddress(
            LUA,
            label,
            icvarVtable[i]
        );
    }

    RawLookupFn raw10 =
        reinterpret_cast<RawLookupFn>(icvarVtable[10]);

    RawLookupFn raw11 =
        reinterpret_cast<RawLookupFn>(icvarVtable[11]);

    RawLookupFn raw12 =
        reinterpret_cast<RawLookupFn>(icvarVtable[12]);

    RawLookupFn raw13 =
        reinterpret_cast<RawLookupFn>(icvarVtable[13]);

    static const char* names[] =
    {
        "r_3dsky",
        "r_spewleaf"
    };

    for (const char* requested : names)
    {
        LogDiagnostic(
            LUA,
            "========== RAW LOOKUP: %s ==========",
            requested
        );

        // ----------------------------------------------------
        // Typed calls for comparison.
        // ----------------------------------------------------

        LogDiagnostic(
            LUA,
            "[raw] Before typed FindCommandBase"
        );

        ConCommandBase* typedBase =
            icvar->FindCommandBase(requested);

        LogPointer(
            LUA,
            "[raw] Typed FindCommandBase",
            typedBase
        );

        LogDiagnostic(
            LUA,
            "[raw] Before typed FindVar"
        );

        ConVar* typedVar =
            icvar->FindVar(requested);

        LogPointer(
            LUA,
            "[raw] Typed FindVar",
            typedVar
        );

        // Const overloads.
        const ICvar* constICvar = icvar;

        const ConCommandBase* typedConstBase =
            constICvar->FindCommandBase(requested);

        const ConVar* typedConstVar =
            constICvar->FindVar(requested);

        LogPointer(
            LUA,
            "[raw] Typed const FindCommandBase",
            typedConstBase
        );

        LogPointer(
            LUA,
            "[raw] Typed const FindVar",
            typedConstVar
        );

        // ----------------------------------------------------
        // Raw vtable calls.
        // ----------------------------------------------------

        LogDiagnostic(
            LUA,
            "[raw] Calling slot 10"
        );

        void* result10 = raw10(
            icvar,
            requested
        );

        LogPointer(
            LUA,
            "[raw] Slot 10 result",
            result10
        );

        LogDiagnostic(
            LUA,
            "[raw] Calling slot 11"
        );

        void* result11 = raw11(
            icvar,
            requested
        );

        LogPointer(
            LUA,
            "[raw] Slot 11 result",
            result11
        );

        LogDiagnostic(
            LUA,
            "[raw] Calling slot 12"
        );

        void* result12 = raw12(
            icvar,
            requested
        );

        LogPointer(
            LUA,
            "[raw] Slot 12 result",
            result12
        );

        LogDiagnostic(
            LUA,
            "[raw] Calling slot 13"
        );

        void* result13 = raw13(
            icvar,
            requested
        );

        LogPointer(
            LUA,
            "[raw] Slot 13 result",
            result13
        );

        // ----------------------------------------------------
        // Compare results.
        // ----------------------------------------------------

        LogDiagnostic(
            LUA,
            "[raw] Slot 10 == slot 12: %s",
            result10 == result12 ? "yes" : "no"
        );

        LogDiagnostic(
            LUA,
            "[raw] Slot 11 == slot 13: %s",
            result11 == result13 ? "yes" : "no"
        );

        LogDiagnostic(
            LUA,
            "[raw] Typed FindVar == raw slot 12: %s",
            typedVar == result12 ? "yes" : "no"
        );

        LogDiagnostic(
            LUA,
            "[raw] Typed FindCommandBase == raw slot 10: %s",
            static_cast<void*>(typedBase) == result10
                ? "yes"
                : "no"
        );

        // ----------------------------------------------------
        // Inspect the correct object returned by raw slot 12.
        // ----------------------------------------------------

        if (result12 == nullptr)
        {
            LogDiagnostic(
                LUA,
                "[raw] Slot 12 returned null; skipping object inspection"
            );

            continue;
        }

        LogPointer(
            LUA,
            "[raw] Returned ConVar object",
            result12
        );

        void** primaryVtable =
            *reinterpret_cast<void***>(result12);

        LogPointer(
            LUA,
            "[raw] Primary vtable",
            primaryVtable
        );

        LogModuleForAddress(
            LUA,
            "[raw] Returned ConVar object",
            result12
        );

        LogModuleForAddress(
            LUA,
            "[raw] Primary vtable",
            primaryVtable
        );

        // Inspect all primary vtable entries.
        for (int i = 0; i < 20; ++i)
        {
            char label[128];

            std::snprintf(
                label,
                sizeof(label),
                "[raw] Primary vtable[%d]",
                i
            );

            LogModuleForAddress(
                LUA,
                label,
                primaryVtable[i]
            );
        }

        // ----------------------------------------------------
        // Primary GetName, called directly through slot 4.
        // ----------------------------------------------------

        using RawGetNameFn = const char* (*)(
            void* self
        );

        RawGetNameFn primaryGetName =
            reinterpret_cast<RawGetNameFn>(
                primaryVtable[4]
            );

        LogDiagnostic(
            LUA,
            "[raw] Before primary raw GetName"
        );

        const char* primaryName =
            primaryGetName(result12);

        LogPointer(
            LUA,
            "[raw] Primary raw GetName result",
            primaryName
        );

        LogMemoryString(
            LUA,
            "[raw] Primary raw name",
            reinterpret_cast<uintptr_t>(primaryName)
        );

        // ----------------------------------------------------
        // Secondary IConVar base.
        // ----------------------------------------------------

        ConVar* returnedConVar =
            reinterpret_cast<ConVar*>(result12);

        IConVar* secondary =
            static_cast<IConVar*>(returnedConVar);

        LogPointer(
            LUA,
            "[raw] Secondary IConVar object",
            secondary
        );

        void** secondaryVtable =
            *reinterpret_cast<void***>(secondary);

        LogPointer(
            LUA,
            "[raw] Secondary vtable",
            secondaryVtable
        );

        LogModuleForAddress(
            LUA,
            "[raw] Secondary IConVar object",
            secondary
        );

        LogModuleForAddress(
            LUA,
            "[raw] Secondary IConVar vtable",
            secondaryVtable
        );

        // Inspect every secondary entry.
        for (int i = 0; i < 5; ++i)
        {
            char label[128];

            std::snprintf(
                label,
                sizeof(label),
                "[raw] Secondary vtable[%d]",
                i
            );

            LogModuleForAddress(
                LUA,
                label,
                secondaryVtable[i]
            );
        }

        // ----------------------------------------------------
        // Secondary GetName, called directly through slot 3.
        // ----------------------------------------------------

        RawGetNameFn secondaryGetName =
            reinterpret_cast<RawGetNameFn>(
                secondaryVtable[3]
            );

        LogDiagnostic(
            LUA,
            "[raw] Before secondary raw GetName"
        );

        const char* secondaryName =
            secondaryGetName(secondary);

        LogPointer(
            LUA,
            "[raw] Secondary raw GetName result",
            secondaryName
        );

        LogMemoryString(
            LUA,
            "[raw] Secondary raw name",
            reinterpret_cast<uintptr_t>(secondaryName)
        );

        // ----------------------------------------------------
        // Inspect name storage without virtual calls.
        // ----------------------------------------------------

        InspectNameFields(
            LUA,
            "[raw] Returned object name",
            result12
        );

        LogDiagnostic(
            LUA,
            "[raw] Primary and secondary name pointers equal: %s",
            primaryName == secondaryName ? "yes" : "no"
        );

        LogDiagnostic(
            LUA,
            "[raw] Object diagnosis completed for %s",
            requested
        );
    }

    LogDiagnostic(
        LUA,
        "[raw] Raw ABI diagnosis completed"
    );

    return 0;
}

// ============================================================
// SetValue
//
// Temporarily disabled while the real setter ABI is being
// identified. This prevents accidental engine crashes.
// ============================================================

LUA_FUNCTION(SetValue)
{
    LogDiagnostic(
        LUA,
        "[icvar] SetValue is temporarily disabled"
    );

    LUA->ThrowError(
        "[icvar] SetValue is temporarily disabled while investigating the engine ABI"
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

    LUA->PushCFunction(DiagnoseLookup);
    LUA->SetField(-2, "DiagnoseLookup");

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
