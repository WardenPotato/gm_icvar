
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <icvar.h>
#include "convar.h"

// Obtain ICvar directly from vstdlib using the version we found.
static ICvar* GetICvar(GarrysMod::Lua::ILuaBase* LUA)
{
    static SourceSDK::FactoryLoader loader("vstdlib");
    static ICvar* icvar = nullptr;
    static bool attempted = false;

    if (!attempted) {
        attempted = true;

        if (loader.IsValid()) {
            icvar = loader.GetInterface<ICvar>("VEngineCvar004");
        }
    }

    if (icvar == nullptr) {
        LUA->ThrowError(
            "[icvar] Failed to obtain VEngineCvar004 from vstdlib"
        );
    }

    return icvar;
}

static ConVar* GetConvar(
    const char* name,
    GarrysMod::Lua::ILuaBase* LUA)
{
    ICvar* icvar = GetICvar(LUA);
    ConVar* convar = icvar->FindVar(name);

    if (convar == nullptr) {
        LUA->ThrowError("[icvar] FindVar returned null for requested convar");
    }

    return convar;
}

LUA_FUNCTION(SetValue)
{
    LUA->CheckType(1, GarrysMod::Lua::Type::STRING);

    const char* name = LUA->GetString(1);
    const int type = LUA->GetType(2);

    ConVar* convar = GetConvar(name, LUA);

    if (type == GarrysMod::Lua::Type::NUMBER) {
        const float value =
            static_cast<float>(LUA->GetNumber(2));

        convar->SetValue(value);
    }
    else if (type == GarrysMod::Lua::Type::STRING) {
        const char* value = LUA->GetString(2);
        convar->SetValue(value);
    }
    else {
        LUA->ArgError(2, "Expected a number or string");
    }

    return 0;
}

GMOD_MODULE_OPEN()
{
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->CreateTable();

    LUA->PushCFunction(SetValue);
    LUA->SetField(-2, "SetValue");

    LUA->SetField(-2, "icvar");
    LUA->Pop();

    return 0;
}
