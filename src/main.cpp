
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <icvar.h>
#include "convar.h"

#include <cstdio>
#include <cstdarg>
#include <cstdint>

// ============================================================
// Persistent diagnostic logging
// ============================================================

static FILE* g_DiagnosticLog = nullptr;

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

    // In-game output.
    if (LUA != nullptr)
        LuaPrint(LUA, message);

    // Persistent output.
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
// Lua output helper
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

static void LogInteger(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    long long value)
{
    LogDiagnostic(
        LUA,
        "[icvar] %s = %lld",
        label,
        value
    );
}

// ============================================================
// Vtable inspection
//
// This reads the object's vtable pointer and a bounded number
// of entries. It does not invoke any virtual functions.
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

    LogDiagnostic(
        LUA,
        "[diag] Reading vtable pointer for %s",
        label
    );

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
// Find a convar
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

    // This is the first engine virtual call in this function.
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

    LogDiagnostic(
        LUA,
        "[icvar] Primary inspection completed"
    );

    return 0;
}

// ============================================================
// Diagnose primary and secondary interface pointers
//
// IMPORTANT:
// - Does not call GetName().
// - Does not call IsFlagSet().
// - Does not call SetValue().
// - Logs every checkpoint to disk.
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
        "[diag 0] Starting diagnosis for %s",
        name
    );

    // --------------------------------------------------------
    // Layer 1: FindVar
    // --------------------------------------------------------

    LogDiagnostic(
        LUA,
        "[diag 1] Before GetConvar"
    );

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    LogDiagnostic(
        LUA,
        "[diag 2] FindVar succeeded"
    );

    // --------------------------------------------------------
    // Layer 2: Primary pointer
    // --------------------------------------------------------

    LogPointer(
        LUA,
        "ConVar primary pointer",
        convar
    );

    LogDiagnostic(
        LUA,
        "[diag 3] Primary pointer logged"
    );

    // --------------------------------------------------------
    // Layer 3: Primary vtable
    // --------------------------------------------------------

    DumpVtable(
        LUA,
        "ConVar primary",
        convar,
        20
    );

    LogDiagnostic(
        LUA,
        "[diag 4] Primary vtable completed"
    );

    // --------------------------------------------------------
    // Layer 4: Secondary base pointer adjustment
    // --------------------------------------------------------

    LogDiagnostic(
        LUA,
        "[diag 5] Before IConVar cast"
    );

    IConVar* iconvar =
        static_cast<IConVar*>(convar);

    LogDiagnostic(
        LUA,
        "[diag 6] After IConVar cast"
    );

    LogPointer(
        LUA,
        "IConVar secondary pointer",
        iconvar
    );

    LogDiagnostic(
        LUA,
        "[diag 7] Secondary pointer logged"
    );

    // --------------------------------------------------------
    // Layer 5: Pointer adjustment amount
    // --------------------------------------------------------

    const std::intptr_t convarAddress =
        reinterpret_cast<std::intptr_t>(convar);

    const std::intptr_t iconvarAddress =
        reinterpret_cast<std::intptr_t>(iconvar);

    LogInteger(
        LUA,
        "IConVar pointer adjustment",
        static_cast<long long>(
            iconvarAddress - convarAddress
        )
    );

    LogDiagnostic(
        LUA,
        "[diag 8] Pointer adjustment calculated"
    );

    // --------------------------------------------------------
    // Layer 6: Secondary vtable
    // --------------------------------------------------------

    DumpVtable(
        LUA,
        "IConVar secondary",
        iconvar,
        5
    );

    LogDiagnostic(
        LUA,
        "[diag 9] Secondary vtable completed"
    );

    // --------------------------------------------------------
    // No virtual calls in this diagnostic pass.
    // --------------------------------------------------------

    LogDiagnostic(
        LUA,
        "[diag 10] Diagnosis completed without virtual calls"
    );

    return 0;
}

// ============================================================
// Original SetValue function
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
