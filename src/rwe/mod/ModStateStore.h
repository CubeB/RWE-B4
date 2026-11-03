#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>
#include <string>
#include <string_view>

namespace rwe
{
    // MOD-08, ADR-0002 D14. Engine-owned, namespaced mod state in global,
    // per-player and per-unit scopes. RWE-original.
    enum class ModValueType
    {
        I32,
        F32,
        String,
        Bytes,
    };

    struct ModValue
    {
        ModValueType type = ModValueType::I32;
        std::int32_t i32 = 0;
        float f32 = 0.0f;
        std::string bytes;

        static ModValue integer(std::int32_t value);
        static ModValue number(float value);
        static ModValue text(std::string value);
        static ModValue binary(std::string value);

        bool operator==(const ModValue& other) const;
    };

    struct ModStateLimits
    {
        // Per namespace, over keys and payloads together.
        std::size_t maxBytesPerMod = 64 * 1024;
        std::size_t maxEntriesPerMod = 4096;
    };

    class ModStateStore
    {
    public:
        explicit ModStateStore(ModStateLimits limits = {});

        // A mod writes only its own namespace. Every read names the namespace
        // it reads, so one mod may read another's published state.
        bool setGlobal(const std::string& mod, const std::string& key, ModValue value);
        std::optional<ModValue> getGlobal(const std::string& mod, const std::string& key) const;

        bool setPlayer(const std::string& mod, PlayerId player, const std::string& key, ModValue value);
        std::optional<ModValue> getPlayer(const std::string& mod, PlayerId player, const std::string& key) const;

        bool setUnit(const std::string& mod, UnitId unit, const std::string& key, ModValue value);
        std::optional<ModValue> getUnit(const std::string& mod, UnitId unit, const std::string& key) const;

        // A unit's entries are deleted with it; a player's with the player.
        void removeUnit(UnitId unit);
        void removePlayer(PlayerId player);
        void clear();

        std::size_t bytesFor(const std::string& mod) const;
        std::size_t entriesFor(const std::string& mod) const;

        // Deterministic canonical form: scopes, then ids, then namespaces,
        // then keys, all in map order.
        nlohmann::json toJson() const;
        // Throws std::runtime_error on malformed or over-quota input.
        void loadJson(const nlohmann::json& value);

        // SHA-256 of the canonical JSON. The hash reads names and values, so
        // two peers with the same store agree; key order never matters.
        std::string hash() const;

    private:
        using Entries = std::map<std::string, ModValue>;

        bool admit(const std::string& mod, const std::string& key, const ModValue& value, const ModValue* replacing) const;
        void accountSet(const std::string& mod, const std::string& key, const ModValue& value, const ModValue* replaced);
        void accountErase(const std::string& mod, const std::string& key, const ModValue& value);

        ModStateLimits limits_;
        std::map<std::string, Entries> globals_;
        std::map<PlayerId, std::map<std::string, Entries>> players_;
        std::map<UnitId, std::map<std::string, Entries>> units_;
        std::map<std::string, std::size_t> bytes_;
        std::map<std::string, std::size_t> entries_;
    };
}
