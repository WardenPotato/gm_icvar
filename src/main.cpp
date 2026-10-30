#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>

#include <cmath>
#include <limits>

#if !defined(_WIN64)
#error gm_icvar raw vtable bindings are intended for 64-bit Windows builds only.
#endif

namespace
{
    // Garry's Mod Main 64-bit interface.
    // Vtable indices are build-specific.
    constexpr const char* kCvarInterfaceVersion = "VEngineCvar004";

    constexpr unsigned int kFindVarSlot = 12;
    constexpr unsigned int kSetStringSlot = 10;
    constexpr unsigned int kSetFloatSlot = 11;
    constexpr unsigned int kSetIntSlot = 12;

    using FindVarFn = void* (*)(
        void* self,
        const char* name
    );

    using SetStringFn = void (*)(
        void* self,
        const char* value
    );

    using SetFloatFn = void (*)(
        void* self,
        float value
    );

    using SetIntFn = void (*)(
        void* self,
        int value
    );

    // ========================================================
    // Obtain ICvar
    // ========================================================

    void* GetICvar(GarrysMod::Lua::ILuaBase* lua)
    {
        static SourceSDK::FactoryLoader loader("vstdlib");
        static void* icvar = nullptr;

        if (icvar != nullptr)
            return icvar;

        if (!loader.IsValid())
        {
            lua->ThrowError(
                "[icvar] Could not load vstdlib"
            );

            return nullptr;
        }

        icvar = loader.GetInterface<void>(
            kCvarInterfaceVersion
        );

        if (icvar == nullptr)
        {
            lua->ThrowError(
                "[icvar] Could not obtain VEngineCvar004"
            );

            return nullptr;
        }

        return icvar;
    }

    // ========================================================
    // Find ConVar using raw ICvar vtable slot 12
    // ========================================================

    void* FindConVar(
        GarrysMod::Lua::ILuaBase* lua,
        const char* name)
    {
        if (name == nullptr || name[0] == '\0')
        {
            lua->ArgError(
                1,
                "Expected a non-empty ConVar name"
            );

            return nullptr;
        }

        void* icvar = GetICvar(lua);

        if (icvar == nullptr)
            return nullptr;

        void** vtable = *reinterpret_cast<void***>(icvar);

        if (vtable == nullptr ||
            vtable[kFindVarSlot] == nullptr)
        {
            lua->ThrowError(
                "[icvar] FindVar vtable entry is unavailable"
            );

            return nullptr;
        }

        const auto findVar =
            reinterpret_cast<FindVarFn>(
                vtable[kFindVarSlot]
            );

        void* convar = findVar(
            icvar,
            name
        );

        if (convar == nullptr)
        {
            lua->ThrowError(
                "[icvar] ConVar was not found"
            );

            return nullptr;
        }

        return convar;
    }

    // ========================================================
    // Obtain primary ConVar vtable
    // ========================================================

    void** GetPrimaryVtable(
        GarrysMod::Lua::ILuaBase* lua,
        void* convar)
    {
        if (convar == nullptr)
            return nullptr;

        void** vtable =
            *reinterpret_cast<void***>(convar);

        if (vtable == nullptr)
        {
            lua->ThrowError(
                "[icvar] ConVar has no vtable"
            );

            return nullptr;
        }

        return vtable;
    }

    // ========================================================
    // SetValue(name, number|string)
    // ========================================================

    LUA_FUNCTION(SetValue)
    {
        LUA->CheckType(
            1,
            GarrysMod::Lua::Type::STRING
        );

        const char* name = LUA->GetString(1);

        const int valueType = LUA->GetType(2);

        if (valueType != GarrysMod::Lua::Type::NUMBER &&
            valueType != GarrysMod::Lua::Type::STRING)
        {
            LUA->ArgError(
                2,
                "Expected a number or string"
            );

            return 0;
        }

        void* convar = FindConVar(
            LUA,
            name
        );

        if (convar == nullptr)
            return 0;

        void** vtable = GetPrimaryVtable(
            LUA,
            convar
        );

        if (vtable == nullptr)
            return 0;

        // ----------------------------------------------------
        // Float setter: primary vtable slot 11
        // ----------------------------------------------------

        if (valueType == GarrysMod::Lua::Type::NUMBER)
        {
            const double number = LUA->GetNumber(2);

            const double maxFloat = static_cast<double>(
                (std::numeric_limits<float>::max)()
            );

            if (!std::isfinite(number) ||
                number > maxFloat ||
                number < -maxFloat)
            {
                LUA->ArgError(
                    2,
                    "Number must be finite and representable as a float"
                );

                return 0;
            }

            if (vtable[kSetFloatSlot] == nullptr)
            {
                LUA->ThrowError(
                    "[icvar] Float setter is unavailable"
                );

                return 0;
            }

            const auto setFloat =
                reinterpret_cast<SetFloatFn>(
                    vtable[kSetFloatSlot]
                );

            setFloat(
                convar,
                static_cast<float>(number)
            );
        }

        // ----------------------------------------------------
        // String setter: primary vtable slot 10
        // ----------------------------------------------------

        else
        {
            const char* value = LUA->GetString(2);

            if (value == nullptr)
            {
                LUA->ArgError(
                    2,
                    "Expected a valid string"
                );

                return 0;
            }

            if (vtable[kSetStringSlot] == nullptr)
            {
                LUA->ThrowError(
                    "[icvar] String setter is unavailable"
                );

                return 0;
            }

            const auto setString =
                reinterpret_cast<SetStringFn>(
                    vtable[kSetStringSlot]
                );

            setString(
                convar,
                value
            );
        }

        return 0;
    }

    // ========================================================
    // SetValueInt(name, integer)
    // ========================================================

    LUA_FUNCTION(SetValueInt)
    {
        LUA->CheckType(
            1,
            GarrysMod::Lua::Type::STRING
        );

        LUA->CheckType(
            2,
            GarrysMod::Lua::Type::NUMBER
        );

        const char* name = LUA->GetString(1);
        const double number = LUA->GetNumber(2);

        if (!std::isfinite(number) ||
            number < static_cast<double>(
                (std::numeric_limits<int>::min)()
            ) ||
            number > static_cast<double>(
                (std::numeric_limits<int>::max)()
            ))
        {
            LUA->ArgError(
                2,
                "Number must be finite and within the integer range"
            );

            return 0;
        }

        void* convar = FindConVar(
            LUA,
            name
        );

        if (convar == nullptr)
            return 0;

        void** vtable = GetPrimaryVtable(
            LUA,
            convar
        );

        if (vtable == nullptr)
            return 0;

        if (vtable[kSetIntSlot] == nullptr)
        {
            LUA->ThrowError(
                "[icvar] Integer setter is unavailable"
            );

            return 0;
        }

        const auto setInt =
            reinterpret_cast<SetIntFn>(
                vtable[kSetIntSlot]
            );

        setInt(
            convar,
            static_cast<int>(number)
        );

        return 0;
    }
}

// ============================================================
// Module initialization
// ============================================================

GMOD_MODULE_OPEN()
{
    LUA->PushSpecial(
        GarrysMod::Lua::SPECIAL_GLOB
    );

    LUA->CreateTable();

    LUA->PushCFunction(SetValue);
    LUA->SetField(-2, "SetValue");

    LUA->PushCFunction(SetValueInt);
    LUA->SetField(-2, "SetValueInt");

    LUA->SetField(
        -2,
        "icvar"
    );

    LUA->Pop();

    return 0;
}

// ============================================================
// Module shutdown
// ============================================================

GMOD_MODULE_CLOSE()
{
    LUA->PushNil();

    LUA->SetField(
        GarrysMod::Lua::SPECIAL_GLOB,
        "icvar"
    );

    return 0;
}