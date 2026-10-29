#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/ModuleLoader.hpp>
#include <GarrysMod/InterfacePointers.hpp>

#include <icvar.h>
#include "convar.h"

ConVar* GetConvar(
    const char* convar_str,
    GarrysMod::Lua::ILuaBase* LUA
) {
    // Diagnostic 1: Check whether the ICvar interface exists.
    ICvar* icvar = InterfacePointers::Cvar();

    if (!icvar) {
        LUA->ThrowError(
            "DIAGNOSTIC: ICvar interface is null"
        );
        return nullptr;
    }

    // Diagnostic 2: Check whether the convar lookup succeeds.
    ConVar* found_cvar = icvar->FindVar(convar_str);

    if (!found_cvar) {
        LUA->ThrowError(
            "DIAGNOSTIC: FindVar returned null"
        );
        return nullptr;
    }

    return found_cvar;
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
