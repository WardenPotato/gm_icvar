
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <icvar.h>
#include "convar.h"

#include <cstdio>

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

// ------------------------------------------------------------
// Inspect a vtable without calling any of its virtual methods.
//
// WARNING: This reads memory through the object's vtable pointer.
// Only use it on pointers returned by the engine's interface
// factory or FindVar(), not arbitrary addresses.
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
//
// We found VEngineCvar004 in the factory scan.
// This avoids InterfacePointers::Cvar(), which requested 007.
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
// Find a convar and log the stages around FindVar().
// ------------------------------------------------------------

static ConVar* GetConvar(
    const char* name,
    GarrysMod::Lua::ILuaBase* LUA)
{
    LuaPrint(LUA, "[icvar] GetConvar: obtaining ICvar");

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

    // If execution crashes here, the ICvar virtual-function
    // layout or the interface itself may be incompatible.
    ConVar* convar = icvar->FindVar(name);

    LogPointer(LUA, "FindVar result", convar);

    if (convar == nullptr) {
        LUA->ThrowError(
            "[icvar] FindVar returned null"
        );
        return nullptr;
    }

    LuaPrint(LUA, "[icvar] FindVar completed successfully");

    return convar;
}

// ------------------------------------------------------------
// List candidate interface versions exposed by vstdlib.
// This does not call methods on the returned interfaces.
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

        void* interface_pointer =
            loader.GetInterface<void>(name);

        if (interface_pointer != nullptr) {
            std::snprintf(
                message,
                sizeof(message),
                "[icvar] FOUND %s at %p",
                name,
                interface_pointer
            );

            LuaPrint(LUA, message);
            ++found;

            if (i == 4) {
                DumpVtable(
                    LUA,
                    "VEngineCvar004",
                    interface_pointer,
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
// Inspect a convar's pointer and vtable without calling
// ConVar::SetValue() or other ConVar virtual methods.
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

    DumpVtable(LUA, "ConVar", convar, 32);

    LuaPrint(
        LUA,
        "[icvar] Inspection finished; no ConVar methods were called"
    );

    return 0;
}

// ------------------------------------------------------------
// SetValue with detailed checkpoints.
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

    LuaPrint(LUA, "[icvar] Checkpoint A: before FindVar");

    ConVar* convar = GetConvar(name, LUA);

    if (convar == nullptr)
        return 0;

    LuaPrint(LUA, "[icvar] Checkpoint B: FindVar returned");

    LogPointer(LUA, "ConVar object", convar);

    // Capture the vtable before the call, in case the crash
    // occurs inside SetValue().
    DumpVtable(LUA, "ConVar before SetValue", convar, 32);

    if (type == GarrysMod::Lua::Type::NUMBER) {
        const double lua_value = LUA->GetNumber(2);
        const float value = static_cast<float>(lua_value);

        std::snprintf(
            message,
            sizeof(message),
            "[icvar] Checkpoint C: number input %.9g; float %.9g",
            lua_value,
            static_cast<double>(value)
        );

        LuaPrint(LUA, message);

        LuaPrint(
            LUA,
            "[icvar] Checkpoint D: calling SetValue(float)"
        );

        convar->SetValue(value);

        LuaPrint(
            LUA,
            "[icvar] Checkpoint E: SetValue(float) returned"
        );
    }
    else if (type == GarrysMod::Lua::Type::STRING) {
        const char* value = LUA->GetString(2);

        std::snprintf(
            message,
            sizeof(message),
            "[icvar] Checkpoint C: string input \"%s\"",
            value != nullptr ? value : "(null)"
        );

        LuaPrint(LUA, message);

        LuaPrint(
            LUA,
            "[icvar] Checkpoint D: calling SetValue(string)"
        );

        convar->SetValue(value);

        LuaPrint(
            LUA,
            "[icvar] Checkpoint E: SetValue(string) returned"
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
// Register the diagnostic and original Lua functions.
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

    LUA->SetField(-2, "icvar");
    LUA->Pop();

    return 0;
}
