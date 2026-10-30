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
// Diagnostic logging
// ============================================================

static FILE* g_DiagnosticLog = nullptr;

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
    char message[1024] = {};

    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (LUA != nullptr)
        LuaPrint(LUA, message);

    InitDiagnosticLog();

    if (g_DiagnosticLog != nullptr)
    {
        std::fprintf(g_DiagnosticLog, "%s\n", message);
        std::fflush(g_DiagnosticLog);
    }
}

static void LogPointer(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* pointer)
{
    LogDiagnostic(LUA, "%s = %p", label, pointer);
}

// ============================================================
// Safe memory inspection helpers
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

static void LogMemoryString(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    uintptr_t address)
{
    if (address == 0)
    {
        LogDiagnostic(LUA, "[memory] %s: nullptr", label);
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
    LogDiagnostic(LUA, "[memory] %s bytes: %s", label, hex);
}

// In the engine.dll ConVar implementation examined so far:
//   +0x18 = name pointer
//   +0x28 = flags
//   +0x38 = self pointer used by the IConVar thunks
// These offsets are diagnostic only; setter calls do not depend on them.
static void InspectNameFields(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object)
{
    if (object == nullptr)
        return;

    const uintptr_t address = reinterpret_cast<uintptr_t>(object);
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

    LogPointer(LUA, "[memory] Name owner", reinterpret_cast<void*>(owner));
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

    LogMemoryString(LUA, label, namePointer);
}

// ============================================================
// Module/RVA diagnostics
// ============================================================

static void LogModuleForAddress(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* address)
{
    if (address == nullptr)
    {
        LogDiagnostic(LUA, "[module] %s: address is null", label);
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
    const DWORD pathLength = GetModuleFileNameA(module, path, MAX_PATH);
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const uintptr_t addr = reinterpret_cast<uintptr_t>(address);

    LogDiagnostic(
        LUA,
        "[module] %s path: %s",
        label,
        pathLength != 0 ? path : "<unavailable>"
    );
    LogDiagnostic(LUA, "[module] %s base: %p", label, module);
    LogDiagnostic(LUA, "[module] %s address: %p", label, address);
    LogDiagnostic(
        LUA,
        "[module] %s RVA: 0x%llX",
        label,
        static_cast<unsigned long long>(addr - base)
    );
}

static void DumpVtable(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int count)
{
    if (object == nullptr)
    {
        LogDiagnostic(LUA, "[vtable] %s: object is null", label);
        return;
    }

    void** vtable = *reinterpret_cast<void***>(const_cast<void*>(object));
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
}

static void InspectObjectModules(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int primaryCount,
    bool inspectSecondary)
{
    if (object == nullptr)
        return;

    void** primaryVtable = *reinterpret_cast<void***>(const_cast<void*>(object));

    LogDiagnostic(LUA, "[module] ===== %s =====", label);
    LogModuleForAddress(LUA, "Object", object);
    LogModuleForAddress(LUA, "Primary vtable", primaryVtable);

    if (primaryVtable == nullptr)
        return;

    for (int i = 0; i < primaryCount; ++i)
    {
        char labelBuffer[128] = {};
        std::snprintf(
            labelBuffer,
            sizeof(labelBuffer),
            "%s primary vtable[%d]",
            label,
            i
        );
        LogModuleForAddress(LUA, labelBuffer, primaryVtable[i]);
    }

    if (!inspectSecondary)
        return;

    const ConVar* constVar = static_cast<const ConVar*>(object);
    const IConVar* secondary = static_cast<const IConVar*>(constVar);
    void** secondaryVtable = *reinterpret_cast<void***>(
        const_cast<IConVar*>(secondary)
    );

    LogModuleForAddress(LUA, "Secondary IConVar object", secondary);
    LogModuleForAddress(LUA, "Secondary IConVar vtable", secondaryVtable);

    if (secondaryVtable == nullptr)
        return;

    for (int i = 0; i < 5; ++i)
    {
        char labelBuffer[128] = {};
        std::snprintf(
            labelBuffer,
            sizeof(labelBuffer),
            "%s secondary vtable[%d]",
            label,
            i
        );
        LogModuleForAddress(LUA, labelBuffer, secondaryVtable[i]);
    }
}

// ============================================================
// ICvar acquisition and raw lookup
// ============================================================

static ICvar* GetICvar(GarrysMod::Lua::ILuaBase* LUA)
{
    static SourceSDK::FactoryLoader loader("vstdlib");
    static ICvar* icvar = nullptr;
    static bool attempted = false;

    if (!attempted)
    {
        attempted = true;
        LogDiagnostic(LUA, "[icvar] Initializing VEngineCvar004");

        if (!loader.IsValid())
        {
            LogDiagnostic(LUA, "[icvar] ERROR: vstdlib loader is invalid");
        }
        else
        {
            LogDiagnostic(LUA, "[icvar] vstdlib loader is valid");
            icvar = loader.GetInterface<ICvar>("VEngineCvar004");
            LogPointer(LUA, "[icvar] ICvar interface", icvar);
        }
    }

    if (icvar == nullptr)
    {
        LogDiagnostic(LUA, "[icvar] ERROR: Could not obtain VEngineCvar004");
        LUA->ThrowError("[icvar] Could not obtain VEngineCvar004");
        return nullptr;
    }

    return icvar;
}

using RawLookupFn = void* (*)(void* self, const char* name);

static ConVar* FindVarRaw(ICvar* icvar, const char* name)
{
    if (icvar == nullptr || name == nullptr)
        return nullptr;

    void** vtable = *reinterpret_cast<void***>(icvar);
    if (vtable == nullptr || vtable[12] == nullptr)
        return nullptr;

    RawLookupFn findVar = reinterpret_cast<RawLookupFn>(vtable[12]);
    return reinterpret_cast<ConVar*>(findVar(icvar, name));
}

static ConVar* GetConvar(
    const char* name,
    GarrysMod::Lua::ILuaBase* LUA)
{
    LogDiagnostic(LUA, "[icvar] GetConvar: obtaining ICvar");

    ICvar* icvar = GetICvar(LUA);
    if (icvar == nullptr)
        return nullptr;

    LogDiagnostic(
        LUA,
        "[icvar] Calling raw FindVar slot 12 for '%s'",
        name != nullptr ? name : "<null>"
    );

    ConVar* convar = FindVarRaw(icvar, name);
    LogPointer(LUA, "[icvar] Raw FindVar result", convar);

    if (convar == nullptr)
    {
        LogDiagnostic(LUA, "[icvar] ERROR: Raw FindVar returned null");
        LUA->ThrowError("[icvar] Raw FindVar returned null");
        return nullptr;
    }

    LogDiagnostic(LUA, "[icvar] Raw FindVar completed successfully");
    return convar;
}

// ============================================================
// Interface scan
// ============================================================

LUA_FUNCTION(ListCvarInterfaces)
{
    LogDiagnostic(LUA, "[icvar] === Interface factory scan started ===");

    SourceSDK::FactoryLoader loader("vstdlib");
    if (!loader.IsValid())
    {
        LogDiagnostic(LUA, "[icvar] ERROR: vstdlib loader is invalid");
        return 0;
    }

    int found = 0;
    char name[64] = {};

    for (int i = 1; i <= 99; ++i)
    {
        std::snprintf(name, sizeof(name), "VEngineCvar%03d", i);
        void* pointer = loader.GetInterface<void>(name);

        if (pointer != nullptr)
        {
            ++found;
            LogDiagnostic(LUA, "[icvar] FOUND %s at %p", name, pointer);

            if (i == 4)
            {
                DumpVtable(LUA, "VEngineCvar004", pointer, 24);
                LogModuleForAddress(LUA, "ICvar object", pointer);
                LogModuleForAddress(
                    LUA,
                    "ICvar vtable",
                    *reinterpret_cast<void**>(pointer)
                );
            }
        }
    }

    LogDiagnostic(LUA, "[icvar] === Scan finished: %d candidates ===", found);
    return 0;
}

// ============================================================
// ConVar inspection
// ============================================================

LUA_FUNCTION(InspectConVar)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* name = LUA->GetString(1);
    ConVar* convar = GetConvar(name, LUA);
    if (convar == nullptr)
        return 0;

    DumpVtable(LUA, "ConVar primary", convar, 20);
    LogModuleForAddress(LUA, "ConVar object", convar);
    LogModuleForAddress(
        LUA,
        "ConVar primary vtable",
        *reinterpret_cast<void**>(convar)
    );

    return 0;
}

LUA_FUNCTION(DiagnoseConVar)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    char name[256] = {};
    const char* input = LUA->GetString(1);
    std::snprintf(name, sizeof(name), "%s", input != nullptr ? input : "");

    LogDiagnostic(LUA, "[diag] Starting ConVar diagnosis: '%s'", name);

    ConVar* convar = GetConvar(name, LUA);
    if (convar == nullptr)
        return 0;

    IConVar* iconvar = static_cast<IConVar*>(convar);

    LogPointer(LUA, "[diag] Primary ConVar", convar);
    LogPointer(LUA, "[diag] Secondary IConVar", iconvar);

    const uintptr_t primary = reinterpret_cast<uintptr_t>(convar);
    const uintptr_t secondary = reinterpret_cast<uintptr_t>(iconvar);

    LogDiagnostic(
        LUA,
        "[diag] Secondary base adjustment: 0x%llX",
        static_cast<unsigned long long>(secondary - primary)
    );

    InspectNameFields(LUA, "[diag] ConVar name", convar);

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

    DumpVtable(LUA, "ConVar primary", convar, 20);
    DumpVtable(LUA, "IConVar secondary", iconvar, 5);
    InspectObjectModules(LUA, "ConVar", convar, 20, true);

    LogDiagnostic(LUA, "[diag] ConVar diagnosis completed");
    return 0;
}

// ============================================================
// Raw lookup diagnostics
// ============================================================

LUA_FUNCTION(DiagnoseLookup)
{
    ICvar* icvar = GetICvar(LUA);
    if (icvar == nullptr)
        return 0;

    void** vtable = *reinterpret_cast<void***>(icvar);
    if (vtable == nullptr)
        return 0;

    LogPointer(LUA, "[raw] ICvar object", icvar);
    LogPointer(LUA, "[raw] ICvar vtable", vtable);

    RawLookupFn raw10 = reinterpret_cast<RawLookupFn>(vtable[10]);
    RawLookupFn raw11 = reinterpret_cast<RawLookupFn>(vtable[11]);
    RawLookupFn raw12 = reinterpret_cast<RawLookupFn>(vtable[12]);
    RawLookupFn raw13 = reinterpret_cast<RawLookupFn>(vtable[13]);

    for (int i = 10; i <= 13; ++i)
    {
        char label[64] = {};
        std::snprintf(label, sizeof(label), "ICvar vtable[%d]", i);
        LogModuleForAddress(LUA, label, vtable[i]);
    }

    static const char* names[] = { "r_3dsky", "r_spewleaf" };

    for (const char* requested : names)
    {
        LogDiagnostic(LUA, "========== RAW LOOKUP: %s ==========", requested);

        void* result10 = raw10(icvar, requested);
        void* result11 = raw11(icvar, requested);
        void* result12 = raw12(icvar, requested);
        void* result13 = raw13(icvar, requested);

        LogPointer(LUA, "[raw] Slot 10 result", result10);
        LogPointer(LUA, "[raw] Slot 11 result", result11);
        LogPointer(LUA, "[raw] Slot 12 result", result12);
        LogPointer(LUA, "[raw] Slot 13 result", result13);

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

        if (result12 == nullptr)
            continue;

        ConVar* convar = reinterpret_cast<ConVar*>(result12);
        IConVar* iconvar = static_cast<IConVar*>(convar);

        void** primaryVtable = *reinterpret_cast<void***>(convar);
        void** secondaryVtable = *reinterpret_cast<void***>(iconvar);

        LogPointer(LUA, "[raw] Returned ConVar object", convar);
        LogPointer(LUA, "[raw] Primary vtable", primaryVtable);
        LogPointer(LUA, "[raw] Secondary IConVar object", iconvar);
        LogPointer(LUA, "[raw] Secondary vtable", secondaryVtable);

        InspectNameFields(LUA, "[raw] Returned object name", convar);
        InspectObjectModules(LUA, "Returned ConVar", convar, 20, true);

        // Read-only raw getter calls, using the observed vtable slots.
        using RawGetNameFn = const char* (*)(void* self);

        RawGetNameFn primaryGetName =
            reinterpret_cast<RawGetNameFn>(primaryVtable[4]);
        RawGetNameFn secondaryGetName =
            reinterpret_cast<RawGetNameFn>(secondaryVtable[3]);

        const char* primaryName = primaryGetName(convar);
        const char* secondaryName = secondaryGetName(iconvar);

        LogPointer(LUA, "[raw] Primary raw GetName result", primaryName);
        LogPointer(LUA, "[raw] Secondary raw GetName result", secondaryName);

        LogMemoryString(
            LUA,
            "[raw] Primary raw name",
            reinterpret_cast<uintptr_t>(primaryName)
        );
        LogMemoryString(
            LUA,
            "[raw] Secondary raw name",
            reinterpret_cast<uintptr_t>(secondaryName)
        );

        LogDiagnostic(
            LUA,
            "[raw] Primary/secondary name pointers equal: %s",
            primaryName == secondaryName ? "yes" : "no"
        );
    }

    LogDiagnostic(LUA, "[raw] Raw lookup diagnosis completed");
    return 0;
}

// ============================================================
// Raw SetValue calls
//
// Primary ConVar vtable layout observed in the runtime:
//   slot 10 = SetValue(const char*)
//   slot 11 = SetValue(float)
//   slot 12 = SetValue(int)
//
// The function pointer signatures explicitly include `self` as
// the first argument. On Windows x64 this places self in RCX;
// the second argument is passed in RDX or XMM1 as appropriate.
// ============================================================

using RawSetStringFn = void (*)(void* self, const char* value);
using RawSetFloatFn  = void (*)(void* self, float value);
using RawSetIntFn    = void (*)(void* self, int value);

LUA_FUNCTION(SetValue)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* name = LUA->GetString(1);
    const int valueType = LUA->GetType(2);

    if (name == nullptr)
    {
        LUA->ThrowError("[raw-set] ConVar name is null");
        return 0;
    }

    LogDiagnostic(LUA, "[raw-set] Requested variable: %s", name);

    // Uses raw ICvar vtable slot 12 through GetConvar().
    ConVar* convar = GetConvar(name, LUA);
    if (convar == nullptr)
        return 0;

    void** vtable = *reinterpret_cast<void***>(convar);
    if (vtable == nullptr)
    {
        LUA->ThrowError("[raw-set] ConVar has a null vtable");
        return 0;
    }

    if (valueType == GarrysMod::Lua::Type::NUMBER)
    {
        const float value = static_cast<float>(LUA->GetNumber(2));
        void* target = vtable[11];

        LogPointer(LUA, "[raw-set] Float setter target", target);
        LogModuleForAddress(LUA, "[raw-set] Float setter", target);
        LogDiagnostic(
            LUA,
            "[raw-set] Calling float setter with %.9g",
            static_cast<double>(value)
        );

        RawSetFloatFn setter = reinterpret_cast<RawSetFloatFn>(target);
        setter(static_cast<void*>(convar), value);

        LogDiagnostic(LUA, "[raw-set] Float setter returned");
    }
    else if (valueType == GarrysMod::Lua::Type::STRING)
    {
        const char* value = LUA->GetString(2);
        if (value == nullptr)
        {
            LUA->ThrowError("[raw-set] String value is null");
            return 0;
        }

        void* target = vtable[10];

        LogPointer(LUA, "[raw-set] String setter target", target);
        LogModuleForAddress(LUA, "[raw-set] String setter", target);
        LogDiagnostic(LUA, "[raw-set] Calling string setter with '%s'", value);

        RawSetStringFn setter = reinterpret_cast<RawSetStringFn>(target);
        setter(static_cast<void*>(convar), value);

        LogDiagnostic(LUA, "[raw-set] String setter returned");
    }
    else
    {
        LUA->ArgError(2, "Expected a number or string");
        return 0;
    }

    LogDiagnostic(LUA, "[raw-set] SetValue completed");
    return 0;
}

// Optional direct integer-setter entry point for testing.
// Lua numbers normally use the float setter above; this function
// allows an explicit integer path when needed.
LUA_FUNCTION(SetValueInt)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);
    LUA->CheckType(2, GarrysMod::Lua::Type::NUMBER);

    const char* name = LUA->GetString(1);
    const int value = static_cast<int>(LUA->GetNumber(2));

    ConVar* convar = GetConvar(name, LUA);
    if (convar == nullptr)
        return 0;

    void** vtable = *reinterpret_cast<void***>(convar);
    if (vtable == nullptr || vtable[12] == nullptr)
    {
        LUA->ThrowError("[raw-set] Integer setter target is null");
        return 0;
    }

    void* target = vtable[12];
    LogPointer(LUA, "[raw-set] Integer setter target", target);
    LogModuleForAddress(LUA, "[raw-set] Integer setter", target);
    LogDiagnostic(LUA, "[raw-set] Calling integer setter with %d", value);

    RawSetIntFn setter = reinterpret_cast<RawSetIntFn>(target);
    setter(static_cast<void*>(convar), value);

    LogDiagnostic(LUA, "[raw-set] Integer setter returned");
    return 0;
}

// ============================================================
// Module entry point
// ============================================================

GMOD_MODULE_OPEN()
{
    InitDiagnosticLog();
    LogDiagnostic(LUA, "[icvar] Module loaded");

    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->CreateTable();

    LUA->PushCFunction(SetValue);
    LUA->SetField(-2, "SetValue");

    LUA->PushCFunction(SetValueInt);
    LUA->SetField(-2, "SetValueInt");

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
    LogDiagnostic(LUA, "[icvar] Module closing");

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
