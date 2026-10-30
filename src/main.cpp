
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <icvar.h>
#include "convar.h"

#include <cstdio>
#include <cstdint>

// ------------------------------------------------------------
// Diagnostic output through Garry's Mod's Lua print().
// ------------------------------------------------------------

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

static void LogPointer(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* pointer)
{
    char message[256];

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] %s = %p",
        label,
        pointer
    );

    LuaPrint(LUA, message);
}

static void LogInteger(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    long long value)
{
    char message[256];

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] %s = %lld",
        label,
        value
    );

    LuaPrint(LUA, message);
}

// ------------------------------------------------------------
// Inspect a vtable without invoking its entries.
// ------------------------------------------------------------

static void DumpVtable(
    GarrysMod::Lua::ILuaBase* LUA,
    const char* label,
    const void* object,
    int count)
{
    if (object == nullptr) {
        LuaPrint(LUA, "[icvar] Cannot inspect a null object");
        return;
    }

    char message[256];

    void** vtable = *reinterpret_cast<void***>(
        const_cast<void*>(object)
    );

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] %s object=%p vtable=%p",
        label,
        object,
        static_cast<void*>(vtable)
    );

    LuaPrint(LUA, message);

    if (vtable == nullptr) {
        LuaPrint(LUA, "[icvar] ERROR: null vtable");
        return;
    }

    for (int i = 0; i < count; ++i) {
        std::snprintf(
            message,
            sizeof(message),
            "[icvar] %s vtable[%02d] = %p",
            label,
            i,
            vtable[i]
        );

        LuaPrint(LUA, message);
    }
}

// ------------------------------------------------------------
// Obtain ICvar directly from vstdlib.
// ------------------------------------------------------------

static ICvar* GetICvar(
    GarrysMod::Lua::ILuaBase* LUA)
{
    static SourceSDK::FactoryLoader loader("vstdlib");
    static ICvar* icvar = nullptr;
    static bool attempted = false;

    if (!attempted) {
        attempted = true;

        LuaPrint(LUA, "[icvar] Initializing VEngineCvar004");

        if (!loader.IsValid()) {
            LuaPrint(
                LUA,
                "[icvar] ERROR: vstdlib loader is invalid"
            );
        }
        else {
            icvar = loader.GetInterface<ICvar>(
                "VEngineCvar004"
            );

            LogPointer(LUA, "ICvar interface", icvar);
        }
    }

    if (icvar == nullptr) {
        LUA->ThrowError(
            "[icvar] Could not obtain VEngineCvar004"
        );
        return nullptr;
    }

    return icvar;
}

// ------------------------------------------------------------
// Find a convar.
// ------------------------------------------------------------

static ConVar* GetConvar(
    const char* name,
    GarrysMod::Lua::ILuaBase* LUA)
{
    LuaPrint(LUA, "[icvar] Obtaining ICvar");

    ICvar* icvar = GetICvar(LUA);

    if (icvar == nullptr)
        return nullptr;

    LogPointer(LUA, "ICvar before FindVar", icvar);

    char message[256];

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] Calling FindVar(\"%s\")",
        name
    );

    LuaPrint(LUA, message);

    ConVar* convar = icvar->FindVar(name);

    LogPointer(LUA, "FindVar result", convar);

    if (convar == nullptr) {
        LUA->ThrowError("[icvar] FindVar returned null");
        return nullptr;
    }

    LuaPrint(LUA, "[icvar] FindVar completed successfully");

    return convar;
}

// ------------------------------------------------------------
// Scan candidate interface versions.
// ------------------------------------------------------------

LUA_FUNCTION(ListCvarInterfaces)
{
    LuaPrint(
        LUA,
        "[icvar] === Interface factory scan started ==="
    );

    SourceSDK::FactoryLoader loader("vstdlib");

    if (!loader.IsValid()) {
        LuaPrint(
            LUA,
            "[icvar] ERROR: vstdlib or CreateInterface unavailable"
        );
        return 0;
    }

    LuaPrint(LUA, "[icvar] vstdlib loader is valid");

    int found = 0;
    char name[64];
    char message[256];

    for (int i = 1; i <= 99; ++i) {
        std::snprintf(
            name,
            sizeof(name),
            "VEngineCvar%03d",
            i
        );

        void* interfacePointer =
            loader.GetInterface<void>(name);

        if (interfacePointer != nullptr) {
            std::snprintf(
                message,
                sizeof(message),
                "[icvar] FOUND %s at %p",
                name,
                interfacePointer
            );

            LuaPrint(LUA, message);
            ++found;

            if (i == 4) {
                DumpVtable(
                    LUA,
                    "VEngineCvar004",
                    interfacePointer,
                    24
                );
            }
        }
    }

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] === Scan finished: %d candidates ===",
        found
    );

    LuaPrint(LUA, message);

    return 0;
}

// ------------------------------------------------------------
// Inspect the primary ConVar vtable.
// ------------------------------------------------------------

LUA_FUNCTION(InspectConVar)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* name = LUA->GetString(1);

    char message[256];

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] === InspectConVar(\"%s\") ===",
        name
    );

    LuaPrint(LUA, message);

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    DumpVtable(
        LUA,
        "ConVar primary",
        convar,
        20
    );

    LuaPrint(LUA, "[icvar] Primary inspection complete");

    return 0;
}

// ------------------------------------------------------------
// Inspect both ConVar and IConVar subobjects.
//
// This function does not call any setter.
// ------------------------------------------------------------

LUA_FUNCTION(DiagnoseConVar)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* name = LUA->GetString(1);

    char message[256];

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] === DiagnoseConVar(\"%s\") ===",
        name
    );

    LuaPrint(LUA, message);

    // Layer 1: FindVar.
    LuaPrint(LUA, "[layer 1] Finding ConVar");

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    // Layer 2: Obtain the secondary base pointer.
    LuaPrint(LUA, "[layer 2] Obtaining IConVar subobject");

    IConVar* iconvar = static_cast<IConVar*>(convar);

    LogPointer(LUA, "ConVar*", convar);
    LogPointer(LUA, "IConVar*", iconvar);

    const auto convarAddress =
        reinterpret_cast<std::intptr_t>(convar);

    const auto iconvarAddress =
        reinterpret_cast<std::intptr_t>(iconvar);

    LogInteger(
        LUA,
        "IConVar pointer adjustment",
        static_cast<long long>(iconvarAddress - convarAddress)
    );

    // Layer 3: Dump the primary vtable.
    LuaPrint(LUA, "[layer 3] Primary vtable");

    DumpVtable(
        LUA,
        "ConVar primary",
        convar,
        20
    );

    // Layer 4: Dump the secondary vtable.
    LuaPrint(LUA, "[layer 4] IConVar vtable");

    DumpVtable(
        LUA,
        "IConVar secondary",
        iconvar,
        5
    );

    // Layer 5: Test non-mutating virtual calls.
    // These are declared in the supplied IConVar header:
    //
    // 0 SetValue(const char*)
    // 1 SetValue(float)
    // 2 SetValue(int)
    // 3 GetName() const
    // 4 IsFlagSet(int) const
    //
    // No setter is invoked here.

    LuaPrint(LUA, "[layer 5] Calling IConVar::GetName");

    const char* actualName = iconvar->GetName();

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] IConVar::GetName returned: %s",
        actualName != nullptr ? actualName : "<null>"
    );

    LuaPrint(LUA, message);

    LuaPrint(LUA, "[layer 5] Calling IConVar::IsFlagSet(0)");

    const bool flagsResult = iconvar->IsFlagSet(0);

    LuaPrint(
        LUA,
        flagsResult
            ? "[icvar] IsFlagSet(0) returned true"
            : "[icvar] IsFlagSet(0) returned false"
    );

    LuaPrint(LUA, "[icvar] === Diagnosis complete ===");

    return 0;
}

// ------------------------------------------------------------
// Original SetValue function.
//
// Retained for compatibility, but not used by the diagnostic.
// ------------------------------------------------------------

LUA_FUNCTION(SetValue)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* name = LUA->GetString(1);
    const int type = LUA->GetType(2);

    char message[256];

    std::snprintf(
        message,
        sizeof(message),
        "[icvar] === SetValue(\"%s\") started ===",
        name
    );

    LuaPrint(LUA, message);

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    if (type == GarrysMod::Lua::Type::NUMBER) {
        const float value =
            static_cast<float>(LUA->GetNumber(2));

        LuaPrint(
            LUA,
            "[icvar] Calling ConVar::SetValue(float)"
        );

        convar->SetValue(value);

        LuaPrint(
            LUA,
            "[icvar] ConVar::SetValue(float) returned"
        );
    }
    else if (type == GarrysMod::Lua::Type::STRING) {
        const char* value = LUA->GetString(2);

        LuaPrint(
            LUA,
            "[icvar] Calling ConVar::SetValue(string)"
        );

        convar->SetValue(value);

        LuaPrint(
            LUA,
            "[icvar] ConVar::SetValue(string) returned"
        );
    }
    else {
        LUA->ArgError(
            2,
            "Expected a number or string"
        );

        return 0;
    }

    LuaPrint(LUA, "[icvar] === SetValue completed ===");

    return 0;
}

// ------------------------------------------------------------
// Module entry points.
// ------------------------------------------------------------

GMOD_MODULE_OPEN()
{
    LuaPrint(LUA, "[icvar] Module loaded");

    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
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

GMOD_MODULE_CLOSE()
{
    LUA->PushNil();
    LUA->SetField(
        GarrysMod::Lua::SPECIAL_GLOB,
        "icvar"
    );

    return 0;
}
