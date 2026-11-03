#include "ModStateStore.h"

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <rwe/util/Sha256.h>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace rwe
{
    namespace
    {
        constexpr int storeFormatVersion = 1;

        std::size_t payloadBytes(const ModValue& value)
        {
            switch (value.type)
            {
            case ModValueType::I32:
            case ModValueType::F32:
                return 4;
            case ModValueType::String:
            case ModValueType::Bytes:
                return value.bytes.size();
            }
            return 0;
        }

        std::size_t entryBytes(const std::string& key, const ModValue& value)
        {
            return key.size() + 8 + payloadBytes(value);
        }

        std::string toHex(const std::string& bytes)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string result;
            result.reserve(bytes.size() * 2);
            for (const auto character : bytes)
            {
                const auto byte = static_cast<unsigned char>(character);
                result.push_back(digits[byte >> 4]);
                result.push_back(digits[byte & 0xf]);
            }
            return result;
        }

        std::string fromHex(const std::string& text)
        {
            if (text.size() % 2 != 0)
            {
                throw std::runtime_error("mod state: bytes value has an odd number of hex digits");
            }
            const auto nibble = [](char character) -> unsigned char {
                if (character >= '0' && character <= '9')
                {
                    return static_cast<unsigned char>(character - '0');
                }
                if (character >= 'a' && character <= 'f')
                {
                    return static_cast<unsigned char>(character - 'a' + 10);
                }
                if (character >= 'A' && character <= 'F')
                {
                    return static_cast<unsigned char>(character - 'A' + 10);
                }
                throw std::runtime_error("mod state: bytes value is not hex");
            };
            std::string result;
            result.reserve(text.size() / 2);
            for (std::size_t i = 0; i < text.size(); i += 2)
            {
                result.push_back(static_cast<char>((nibble(text[i]) << 4) | nibble(text[i + 1])));
            }
            return result;
        }

        json encode(const ModValue& value)
        {
            switch (value.type)
            {
            case ModValueType::I32:
                return json{{"type", "i32"}, {"value", value.i32}};
            case ModValueType::F32:
                return json{{"type", "f32"}, {"value", value.f32}};
            case ModValueType::String:
                return json{{"type", "string"}, {"value", value.bytes}};
            case ModValueType::Bytes:
                return json{{"type", "bytes"}, {"value", toHex(value.bytes)}};
            }
            return nullptr;
        }

        ModValue decode(const json& value)
        {
            if (!value.is_object() || !value.contains("type") || !value["type"].is_string()
                || !value.contains("value"))
            {
                throw std::runtime_error("mod state: an entry needs a string type and a value");
            }
            const auto type = value["type"].get<std::string>();
            if (type == "i32")
            {
                if (!value["value"].is_number_integer())
                {
                    throw std::runtime_error("mod state: an i32 entry needs an integer value");
                }
                const auto number = value["value"].get<long long>();
                if (number < std::numeric_limits<std::int32_t>::min()
                    || number > std::numeric_limits<std::int32_t>::max())
                {
                    throw std::runtime_error("mod state: an i32 entry is out of range");
                }
                return ModValue::integer(static_cast<std::int32_t>(number));
            }
            if (type == "f32")
            {
                if (!value["value"].is_number())
                {
                    throw std::runtime_error("mod state: an f32 entry needs a number value");
                }
                const auto number = value["value"].get<float>();
                if (!std::isfinite(number))
                {
                    throw std::runtime_error("mod state: an f32 entry must be finite");
                }
                return ModValue::number(number);
            }
            if (type == "string")
            {
                if (!value["value"].is_string())
                {
                    throw std::runtime_error("mod state: a string entry needs a string value");
                }
                return ModValue::text(value["value"].get<std::string>());
            }
            if (type == "bytes")
            {
                if (!value["value"].is_string())
                {
                    throw std::runtime_error("mod state: a bytes entry needs a hex string");
                }
                return ModValue::binary(fromHex(value["value"].get<std::string>()));
            }
            throw std::runtime_error("mod state: unknown value type \"" + type + "\"");
        }

        json encodeEntries(const std::map<std::string, ModValue>& entries)
        {
            json result = json::object();
            for (const auto& [key, value] : entries)
            {
                result[key] = encode(value);
            }
            return result;
        }

        std::size_t idValue(PlayerId id)
        {
            return id.value;
        }

        std::size_t idValue(UnitId id)
        {
            return id.value;
        }

        unsigned int parseId(const std::string& text)
        {
            if (text.empty())
            {
                throw std::runtime_error("mod state: an empty scope id");
            }
            std::size_t consumed = 0;
            unsigned long value = 0;
            try
            {
                value = std::stoul(text, &consumed, 10);
            }
            catch (const std::exception&)
            {
                throw std::runtime_error("mod state: scope id \"" + text + "\" is not a number");
            }
            if (consumed != text.size() || value > std::numeric_limits<unsigned int>::max())
            {
                throw std::runtime_error("mod state: scope id \"" + text + "\" is not a valid id");
            }
            return static_cast<unsigned int>(value);
        }
    }

    ModValue ModValue::integer(std::int32_t value)
    {
        ModValue result;
        result.type = ModValueType::I32;
        result.i32 = value;
        return result;
    }

    ModValue ModValue::number(float value)
    {
        ModValue result;
        result.type = ModValueType::F32;
        result.f32 = value;
        return result;
    }

    ModValue ModValue::text(std::string value)
    {
        ModValue result;
        result.type = ModValueType::String;
        result.bytes = std::move(value);
        return result;
    }

    ModValue ModValue::binary(std::string value)
    {
        ModValue result;
        result.type = ModValueType::Bytes;
        result.bytes = std::move(value);
        return result;
    }

    bool ModValue::operator==(const ModValue& other) const
    {
        return type == other.type && i32 == other.i32 && f32 == other.f32 && bytes == other.bytes;
    }

    ModStateStore::ModStateStore(ModStateLimits limits) : limits_(limits)
    {
    }

    bool ModStateStore::admit(
        const std::string& mod,
        const std::string& key,
        const ModValue& value,
        const ModValue* replacing) const
    {
        const auto currentBytes = bytesFor(mod);
        const auto currentEntries = entriesFor(mod);
        const auto nextBytes = currentBytes - (replacing ? entryBytes(key, *replacing) : 0) + entryBytes(key, value);
        const auto nextEntries = currentEntries + (replacing ? 0 : 1);
        return nextBytes <= limits_.maxBytesPerMod && nextEntries <= limits_.maxEntriesPerMod;
    }

    void ModStateStore::accountSet(
        const std::string& mod,
        const std::string& key,
        const ModValue& value,
        const ModValue* replaced)
    {
        auto& bytes = bytes_[mod];
        auto& entries = entries_[mod];
        if (replaced)
        {
            bytes -= entryBytes(key, *replaced);
        }
        else
        {
            ++entries;
        }
        bytes += entryBytes(key, value);
    }

    void ModStateStore::accountErase(const std::string& mod, const std::string& key, const ModValue& value)
    {
        bytes_[mod] -= entryBytes(key, value);
        --entries_[mod];
    }

    bool ModStateStore::setGlobal(const std::string& mod, const std::string& key, ModValue value)
    {
        const ModValue* replacing = nullptr;
        if (const auto namespaceIt = globals_.find(mod); namespaceIt != globals_.end())
        {
            if (const auto existing = namespaceIt->second.find(key); existing != namespaceIt->second.end())
            {
                replacing = &existing->second;
            }
        }
        if (!admit(mod, key, value, replacing))
        {
            return false;
        }
        auto& entries = globals_[mod];
        const auto existing = entries.find(key);
        if (existing == entries.end())
        {
            entries.emplace(key, value);
            accountSet(mod, key, value, nullptr);
        }
        else
        {
            const auto old = existing->second;
            existing->second = std::move(value);
            accountSet(mod, key, existing->second, &old);
        }
        return true;
    }

    std::optional<ModValue> ModStateStore::getGlobal(const std::string& mod, const std::string& key) const
    {
        const auto namespaceIt = globals_.find(mod);
        if (namespaceIt == globals_.end())
        {
            return std::nullopt;
        }
        const auto value = namespaceIt->second.find(key);
        return value == namespaceIt->second.end() ? std::nullopt : std::optional<ModValue>(value->second);
    }

    bool ModStateStore::setPlayer(const std::string& mod, PlayerId player, const std::string& key, ModValue value)
    {
        const ModValue* replacing = nullptr;
        if (const auto playerIt = players_.find(player); playerIt != players_.end())
        {
            if (const auto namespaceIt = playerIt->second.find(mod); namespaceIt != playerIt->second.end())
            {
                if (const auto existing = namespaceIt->second.find(key); existing != namespaceIt->second.end())
                {
                    replacing = &existing->second;
                }
            }
        }
        if (!admit(mod, key, value, replacing))
        {
            return false;
        }
        auto& entries = players_[player][mod];
        const auto existing = entries.find(key);
        if (existing == entries.end())
        {
            entries.emplace(key, value);
            accountSet(mod, key, value, nullptr);
        }
        else
        {
            const auto old = existing->second;
            existing->second = std::move(value);
            accountSet(mod, key, existing->second, &old);
        }
        return true;
    }

    std::optional<ModValue> ModStateStore::getPlayer(const std::string& mod, PlayerId player, const std::string& key) const
    {
        const auto playerIt = players_.find(player);
        if (playerIt == players_.end())
        {
            return std::nullopt;
        }
        const auto namespaceIt = playerIt->second.find(mod);
        if (namespaceIt == playerIt->second.end())
        {
            return std::nullopt;
        }
        const auto value = namespaceIt->second.find(key);
        return value == namespaceIt->second.end() ? std::nullopt : std::optional<ModValue>(value->second);
    }

    bool ModStateStore::setUnit(const std::string& mod, UnitId unit, const std::string& key, ModValue value)
    {
        const ModValue* replacing = nullptr;
        if (const auto unitIt = units_.find(unit); unitIt != units_.end())
        {
            if (const auto namespaceIt = unitIt->second.find(mod); namespaceIt != unitIt->second.end())
            {
                if (const auto existing = namespaceIt->second.find(key); existing != namespaceIt->second.end())
                {
                    replacing = &existing->second;
                }
            }
        }
        if (!admit(mod, key, value, replacing))
        {
            return false;
        }
        auto& entries = units_[unit][mod];
        const auto existing = entries.find(key);
        if (existing == entries.end())
        {
            entries.emplace(key, value);
            accountSet(mod, key, value, nullptr);
        }
        else
        {
            const auto old = existing->second;
            existing->second = std::move(value);
            accountSet(mod, key, existing->second, &old);
        }
        return true;
    }

    std::optional<ModValue> ModStateStore::getUnit(const std::string& mod, UnitId unit, const std::string& key) const
    {
        const auto unitIt = units_.find(unit);
        if (unitIt == units_.end())
        {
            return std::nullopt;
        }
        const auto namespaceIt = unitIt->second.find(mod);
        if (namespaceIt == unitIt->second.end())
        {
            return std::nullopt;
        }
        const auto value = namespaceIt->second.find(key);
        return value == namespaceIt->second.end() ? std::nullopt : std::optional<ModValue>(value->second);
    }

    void ModStateStore::removeUnit(UnitId unit)
    {
        const auto unitIt = units_.find(unit);
        if (unitIt == units_.end())
        {
            return;
        }
        for (const auto& [mod, entries] : unitIt->second)
        {
            for (const auto& [key, value] : entries)
            {
                accountErase(mod, key, value);
            }
        }
        units_.erase(unitIt);
    }

    void ModStateStore::removePlayer(PlayerId player)
    {
        const auto playerIt = players_.find(player);
        if (playerIt == players_.end())
        {
            return;
        }
        for (const auto& [mod, entries] : playerIt->second)
        {
            for (const auto& [key, value] : entries)
            {
                accountErase(mod, key, value);
            }
        }
        players_.erase(playerIt);
    }

    void ModStateStore::clear()
    {
        globals_.clear();
        players_.clear();
        units_.clear();
        bytes_.clear();
        entries_.clear();
    }

    std::size_t ModStateStore::bytesFor(const std::string& mod) const
    {
        const auto it = bytes_.find(mod);
        return it == bytes_.end() ? 0 : it->second;
    }

    std::size_t ModStateStore::entriesFor(const std::string& mod) const
    {
        const auto it = entries_.find(mod);
        return it == entries_.end() ? 0 : it->second;
    }

    json ModStateStore::toJson() const
    {
        json root;
        root["format"] = "rwe.mod-state";
        root["version"] = storeFormatVersion;

        json globals = json::object();
        for (const auto& [mod, entries] : globals_)
        {
            globals[mod] = encodeEntries(entries);
        }
        root["globals"] = std::move(globals);

        json players = json::object();
        for (const auto& [player, namespaces] : players_)
        {
            json scoped = json::object();
            for (const auto& [mod, entries] : namespaces)
            {
                scoped[mod] = encodeEntries(entries);
            }
            players[std::to_string(idValue(player))] = std::move(scoped);
        }
        root["players"] = std::move(players);

        json units = json::object();
        for (const auto& [unit, namespaces] : units_)
        {
            json scoped = json::object();
            for (const auto& [mod, entries] : namespaces)
            {
                scoped[mod] = encodeEntries(entries);
            }
            units[std::to_string(idValue(unit))] = std::move(scoped);
        }
        root["units"] = std::move(units);
        return root;
    }

    void ModStateStore::loadJson(const json& value)
    {
        if (!value.is_object() || value.value("format", std::string()) != "rwe.mod-state")
        {
            throw std::runtime_error("mod state: not an rwe.mod-state object");
        }
        if (value.value("version", 0) != storeFormatVersion)
        {
            throw std::runtime_error("mod state: unsupported state version");
        }
        // Parse into a fresh store: a malformed state leaves the current one
        // untouched rather than half-cleared.
        ModStateStore loaded(limits_);
        try
        {
            const auto globals = value.value("globals", json::object());
            for (const auto& [mod, entries] : globals.items())
            {
                for (const auto& [key, entry] : entries.items())
                {
                    if (!loaded.setGlobal(mod, key, decode(entry)))
                    {
                        throw std::runtime_error("mod state: mod \"" + mod + "\" exceeds its quota");
                    }
                }
            }
            const auto players = value.value("players", json::object());
            for (const auto& [playerText, namespaces] : players.items())
            {
                const auto player = PlayerId(parseId(playerText));
                for (const auto& [mod, entries] : namespaces.items())
                {
                    for (const auto& [key, entry] : entries.items())
                    {
                        if (!loaded.setPlayer(mod, player, key, decode(entry)))
                        {
                            throw std::runtime_error("mod state: mod \"" + mod + "\" exceeds its quota");
                        }
                    }
                }
            }
            const auto units = value.value("units", json::object());
            for (const auto& [unitText, namespaces] : units.items())
            {
                const auto unit = UnitId(parseId(unitText));
                for (const auto& [mod, entries] : namespaces.items())
                {
                    for (const auto& [key, entry] : entries.items())
                    {
                        if (!loaded.setUnit(mod, unit, key, decode(entry)))
                        {
                            throw std::runtime_error("mod state: mod \"" + mod + "\" exceeds its quota");
                        }
                    }
                }
            }
        }
        catch (...)
        {
            throw;
        }
        *this = std::move(loaded);
    }

    std::string ModStateStore::hash() const
    {
        return sha256Hex(toJson().dump(-1, ' ', false));
    }
}

