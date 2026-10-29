#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/ModuleLoader.hpp>
#include <GarrysMod/InterfacePointers.hpp>

#include <icvar.h>
#include "convar.h"

ConVar* GetConvar(
    const char* convar_str,
    GarrysMod::Lua::ILuaBase* LUA
) {
    static SourceSDK::FactoryLoader loader("vstdlib");

    ICvar* icvar =
        loader.GetInterface<ICvar>("VEngineCvar007");

    if (!icvar) {
        LUA->ThrowError(
            "DIAGNOSTIC: vstdlib factory could not provide "
            "VEngineCvar007"
        );
        return nullptr;
    }

    ConVar* convar = icvar->FindVar(convar_str);

    if (!convar) {
        LUA->ThrowError(
            "DIAGNOSTIC: ICvar found, but FindVar returned null"
        );
        return nullptr;
    }

    return convar;
}

LUA_FUNCTION(SetValue) {
    LUA->CheckString(1);

    const int type = LUA->GetType(2);

    if (type != GarrysMod::Lua::Type::Number &&
        type != GarrysMod::Lua::Type::String) {
        LUA->ArgError(2, "Expected a number or string");
        return 0;
    }

    const char* convar_str = LUA->GetString(1);

    ConVar* found_cvar = GetConvar(convar_str, LUA);

    if (!found_cvar) {
        return 0;
    }

    // Diagnostic 3: If execution crashes after this point,
    // investigate the native SetValue call and its ABI.
    if (type == GarrysMod::Lua::Type::Number) {
        const int new_value = LUA->GetNumber(2);
        found_cvar->SetValue(new_value);
    }
    else {
        const char* new_value = LUA->GetString(2);
        found_cvar->SetValue(new_value);
    }

    return 0;
}

GMOD_MODULE_OPEN() {
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->CreateTable();

    LUA->PushCFunction(SetValue);
    LUA->SetField(-2, "SetValue");

    LUA->SetField(-2, "icvar");
    LUA->Pop();

    return 0;
}
