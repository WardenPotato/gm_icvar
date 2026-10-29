
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/InterfacePointers.hpp>

#include <icvar.h>
#include "convar.h"

#include <cstdio>

// Print a diagnostic message through Garry's Mod's Lua print().
static void LuaPrint(GarrysMod::Lua::ILuaBase* LUA, const char* message)
{
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "print");
    LUA->PushString(message);
    LUA->Call(1, 0);
    LUA->Pop();
}

// Scan the vstdlib factory for candidate VEngineCvar interface versions.
// A non-null result only means the factory accepts that interface name.
LUA_FUNCTION(ListCvarInterfaces)
{
    SourceSDK::FactoryLoader loader("vstdlib");

    if (!loader.IsValid()) {
        LuaPrint(LUA,
            "[icvar] ERROR: vstdlib or its CreateInterface export is unavailable");
        return 0;
    }

    LuaPrint(LUA,
        "[icvar] Scanning VEngineCvar001 through VEngineCvar099...");

    int found = 0;
    char name[64];
    char message[160];

    for (int i = 1; i <= 99; ++i) {
        std::snprintf(name, sizeof(name), "VEngineCvar%03d", i);

        void* iface = loader.GetInterface<void>(name);

        if (iface != nullptr) {
            std::snprintf(message, sizeof(message),
                "[icvar] FOUND: %s", name);
            LuaPrint(LUA, message);
            ++found;
        }
    }

    std::snprintf(message, sizeof(message),
        "[icvar] Scan complete. Found %d candidate(s).", found);
    LuaPrint(LUA, message);

    return 0;
}

static ICvar* GetICvar(GarrysMod::Lua::ILuaBase* LUA)
{
    ICvar* icvar = InterfacePointers::Cvar();

    if (icvar == nullptr) {
        LUA->ThrowError(
            "ICvar interface is null. Run icvar.ListCvarInterfaces() "
            "to inspect available versions.");
    }

    return icvar;
}

static ConVar* GetConvar(
    const char* convar_str,
    GarrysMod::Lua::ILuaBase* LUA)
{
    ICvar* icvar = GetICvar(LUA);

    ConVar* found_cvar = icvar->FindVar(convar_str);

    if (found_cvar == nullptr) {
        LUA->ThrowError("Could not find convar");
    }

    return found_cvar;
}

LUA_FUNCTION(SetValue)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* convar_str = LUA->GetString(1);
    const int type = LUA->GetType(2);

    ConVar* found_cvar = GetConvar(convar_str, LUA);

    if (type == GarrysMod::Lua::Type::NUMBER) {
        const float new_value =
            static_cast<float>(LUA->GetNumber(2));

        found_cvar->SetValue(new_value);
    }
    else if (type == GarrysMod::Lua::Type::STRING) {
        const char* new_value = LUA->GetString(2);

        found_cvar->SetValue(new_value);
    }
    else {
        LUA->ArgError(2, "Invalid value type input");
    }

    return 0;
}

GMOD_MODULE_OPEN()
{
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->CreateTable();

    LUA->PushCFunction(SetValue);
    LUA->SetField(-2, "SetValue");

    LUA->PushCFunction(ListCvarInterfaces);
    LUA->SetField(-2, "ListCvarInterfaces");

    LUA->SetField(-2, "icvar");
    LUA->Pop();

    return 0;
}
