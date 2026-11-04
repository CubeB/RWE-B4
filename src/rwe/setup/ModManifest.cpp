#include "ModManifest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace rwe
{
    namespace
    {
        ModManifestError failValue(ModErrorCode code, std::string packageId, std::string detail)
        {
            return {code, std::move(packageId), std::move(detail)};
        }

        // A Result-returning function can `return fail(...)` directly.
        result::Err<ModManifestError> fail(ModErrorCode code, std::string packageId, std::string detail)
        {
            return Err(failValue(code, std::move(packageId), std::move(detail)));
        }

        const char* errorCodeText(ModErrorCode code)
        {
            switch (code)
            {
            case ModErrorCode::InvalidManifest:
                return "invalid-manifest";
            case ModErrorCode::UnsupportedManifestVersion:
                return "unsupported-manifest-version";
            case ModErrorCode::MissingField:
                return "missing-field";
            case ModErrorCode::InvalidField:
                return "invalid-field";
            case ModErrorCode::UnsupportedVersion:
                return "unsupported-version";
            case ModErrorCode::UnsupportedConstraint:
                return "unsupported-constraint";
            case ModErrorCode::UnsupportedApi:
                return "unsupported-api";
            case ModErrorCode::UnsupportedFeature:
                return "unsupported-feature";
            case ModErrorCode::UnsupportedKind:
                return "unsupported-kind";
            case ModErrorCode::DuplicateId:
                return "duplicate-id";
            case ModErrorCode::UnknownPackage:
                return "unknown-package";
            case ModErrorCode::MissingDependency:
                return "missing-dependency";
            case ModErrorCode::UnsatisfiedDependency:
                return "unsatisfied-dependency";
            case ModErrorCode::UnknownOrderingReference:
                return "unknown-ordering-reference";
            case ModErrorCode::DependencyCycle:
                return "dependency-cycle";
            case ModErrorCode::ExclusiveConflict:
                return "exclusive-conflict";
            case ModErrorCode::UnknownOption:
                return "unknown-option";
            case ModErrorCode::InvalidOptionValue:
                return "invalid-option-value";
            case ModErrorCode::MissingOptionValue:
                return "missing-option-value";
            }
            return "unknown";
        }

        bool matches(const std::string& text, const char* pattern)
        {
            return std::regex_match(text, std::regex(pattern));
        }

        bool validId(const std::string& text)
        {
            return matches(text, "^[a-z0-9][a-z0-9._-]{0,127}$");
        }

        bool validFeatureName(const std::string& text)
        {
            return matches(text, "^[a-z][a-z0-9._-]*(@[1-9][0-9]*)?$");
        }

        bool validExclusiveKey(const std::string& text)
        {
            return matches(text, "^[a-z0-9][a-z0-9._:-]{0,63}$");
        }

        bool validOptionKey(const std::string& text)
        {
            return matches(text, "^[a-z][a-z0-9_.]{0,63}$");
        }

        bool validEntryPath(const std::string& text)
        {
            if (text.empty() || text.size() > 260)
            {
                return false;
            }
            if (text.front() == '/' || text.find('\\') != std::string::npos || text.find(':') != std::string::npos
                || text.find('\0') != std::string::npos)
            {
                return false;
            }
            std::size_t start = 0;
            while (start <= text.size())
            {
                const auto end = text.find('/', start);
                const auto component = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (component.empty() || component == "." || component == "..")
                {
                    return false;
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return true;
        }

        std::vector<std::string> splitSpaces(std::string_view text)
        {
            std::vector<std::string> tokens;
            std::size_t start = 0;
            while (start < text.size())
            {
                while (start < text.size() && (text[start] == ' ' || text[start] == '\t'))
                {
                    ++start;
                }
                const auto end = text.find_first_of(" \t", start);
                if (end == std::string_view::npos)
                {
                    tokens.emplace_back(text.substr(start));
                    break;
                }
                tokens.emplace_back(text.substr(start, end - start));
                start = end;
            }
            return tokens;
        }

        struct Comparator
        {
            enum class Op
            {
                Equal,
                Greater,
                GreaterOrEqual,
                Less,
                LessOrEqual,
            };

            Op op = Op::Equal;
            ModVersion version;
        };

        bool compare(const ModVersion& a, const ModVersion& b)
        {
            return a < b;
        }

        bool satisfiesComparator(const ModVersion& value, const Comparator& comparator)
        {
            switch (comparator.op)
            {
            case Comparator::Op::Equal:
                return value == comparator.version;
            case Comparator::Op::Greater:
                return compare(comparator.version, value);
            case Comparator::Op::GreaterOrEqual:
                return !compare(value, comparator.version);
            case Comparator::Op::Less:
                return compare(value, comparator.version);
            case Comparator::Op::LessOrEqual:
                return !compare(comparator.version, value);
            }
            return false;
        }

        Result<std::vector<Comparator>, ModManifestError> parseConstraint(
            const std::string& text,
            const std::string& packageId)
        {
            if (text.empty())
            {
                return fail(ModErrorCode::UnsupportedConstraint, packageId, "an empty version constraint is not a range; use \"*\"");
            }

            std::vector<Comparator> comparators;
            const auto tokens = splitSpaces(text);
            if (tokens.empty())
            {
                return fail(ModErrorCode::UnsupportedConstraint, packageId, "an empty version constraint is not a range; use \"*\"");
            }
            for (const auto& token : tokens)
            {
                if (token == "*")
                {
                    if (tokens.size() != 1)
                    {
                        return fail(ModErrorCode::UnsupportedConstraint, packageId,
                            "\"*\" cannot be combined with another comparator: " + text);
                    }
        return Ok(comparators);
                }

                std::string versionText;
                if (token.size() >= 2 && token.compare(0, 2, ">=") == 0)
                {
                    versionText = token.substr(2);
                    comparators.push_back({Comparator::Op::GreaterOrEqual, {}});
                }
                else if (token.size() >= 2 && token.compare(0, 2, "<=") == 0)
                {
                    versionText = token.substr(2);
                    comparators.push_back({Comparator::Op::LessOrEqual, {}});
                }
                else if (token.size() >= 1 && token[0] == '>')
                {
                    versionText = token.substr(1);
                    comparators.push_back({Comparator::Op::Greater, {}});
                }
                else if (token.size() >= 1 && token[0] == '<')
                {
                    versionText = token.substr(1);
                    comparators.push_back({Comparator::Op::Less, {}});
                }
                else if (token.size() >= 1 && token[0] == '=')
                {
                    versionText = token.substr(1);
                    comparators.push_back({Comparator::Op::Equal, {}});
                }
                else if (token.size() >= 1 && token[0] == '^')
                {
                    const auto parsed = parseModVersion(token.substr(1));
                    if (!parsed)
                    {
        return Err(parsed.getErr());
                    }
                    const auto v = parsed.get();
                    comparators.push_back({Comparator::Op::GreaterOrEqual, v});
                    if (v.major > 0)
                    {
                        comparators.push_back({Comparator::Op::Less, {v.major + 1, 0, 0}});
                    }
                    else if (v.minor > 0)
                    {
                        comparators.push_back({Comparator::Op::Less, {0, v.minor + 1, 0}});
                    }
                    else
                    {
                        comparators.push_back({Comparator::Op::Equal, v});
                    }
                    continue;
                }
                else if (token.size() >= 1 && token[0] == '~')
                {
                    const auto parsed = parseModVersion(token.substr(1));
                    if (!parsed)
                    {
        return Err(parsed.getErr());
                    }
                    const auto v = parsed.get();
                    comparators.push_back({Comparator::Op::GreaterOrEqual, v});
                    comparators.push_back({Comparator::Op::Less, {v.major, v.minor + 1, 0}});
                    continue;
                }
                else
                {
                    versionText = token;
                    comparators.push_back({Comparator::Op::Equal, {}});
                }

                const auto parsed = parseModVersion(versionText);
                if (!parsed)
                {
        return Err(parsed.getErr());
                }
                comparators.back().version = parsed.get();
            }
        return Ok(comparators);
        }

        std::optional<ModManifestError> validateOptionValue(
            const ModOption& option,
            const json& value,
            const std::string& packageId)
        {
            switch (option.type)
            {
            case ModOptionType::Number:
            {
                if (!value.is_number())
                {
                    return failValue(ModErrorCode::InvalidOptionValue, packageId,
                        "option \"" + option.key + "\" must be a number");
                }
                const auto number = value.get<double>();
                if (!std::isfinite(number))
                {
                    return failValue(ModErrorCode::InvalidOptionValue, packageId,
                        "option \"" + option.key + "\" must be finite");
                }
                if ((option.minimum && number < *option.minimum) || (option.maximum && number > *option.maximum))
                {
                    std::ostringstream message;
                    message << "option \"" << option.key << "\" is " << number << ", outside ";
                    if (option.minimum && option.maximum)
                    {
                        message << "[" << *option.minimum << ", " << *option.maximum << "]";
                    }
                    else if (option.minimum)
                    {
                        message << "the minimum " << *option.minimum;
                    }
                    else
                    {
                        message << "the maximum " << *option.maximum;
                    }
                    return failValue(ModErrorCode::InvalidOptionValue, packageId, message.str());
                }
                return std::nullopt;
            }
            case ModOptionType::Boolean:
                if (!value.is_boolean())
                {
                    return failValue(ModErrorCode::InvalidOptionValue, packageId,
                        "option \"" + option.key + "\" must be a boolean");
                }
                return std::nullopt;
            case ModOptionType::String:
                if (!value.is_string())
                {
                    return failValue(ModErrorCode::InvalidOptionValue, packageId,
                        "option \"" + option.key + "\" must be a string");
                }
                if (!option.choices.empty())
                {
                    const auto chosen = value.get<std::string>();
                    if (std::find(option.choices.begin(), option.choices.end(), chosen) == option.choices.end())
                    {
                        std::string allowed;
                        for (const auto& choice : option.choices)
                        {
                            allowed += (allowed.empty() ? "" : ", ") + choice;
                        }
                        return failValue(ModErrorCode::InvalidOptionValue, packageId,
                            "option \"" + option.key + "\" is \"" + chosen + "\"; expected one of " + allowed);
                    }
                }
                return std::nullopt;
            }
            return std::nullopt;
        }

        std::optional<ModManifestError> readStringArray(
            const json& object,
            const char* field,
            const std::string& packageId,
            std::vector<std::string>& out,
            bool (*validator)(const std::string&) = nullptr,
            const char* expectation = nullptr)
        {
            if (!object.contains(field))
            {
                return std::nullopt;
            }
            if (!object[field].is_array())
            {
                return failValue(ModErrorCode::InvalidField, packageId, std::string("\"") + field + "\" must be an array");
            }
            std::set<std::string> seen;
            for (const auto& item : object[field])
            {
                if (!item.is_string())
                {
                    return failValue(ModErrorCode::InvalidField, packageId,
                        std::string("\"") + field + "\" entries must be strings");
                }
                const auto text = item.get<std::string>();
                if (validator && !validator(text))
                {
                    return failValue(ModErrorCode::InvalidField, packageId,
                        std::string("\"") + field + "\" contains \"" + text + "\""
                            + (expectation ? std::string("; expected ") + expectation : std::string()));
                }
                if (!seen.insert(text).second)
                {
                    return failValue(ModErrorCode::InvalidField, packageId,
                        std::string("\"") + field + "\" names \"" + text + "\" twice");
                }
                out.push_back(text);
            }
            return std::nullopt;
        }
    }

    std::string ModManifestError::describe() const
    {
        std::string message = errorCodeText(code);
        if (!packageId.empty())
        {
            message += " [" + packageId + "]";
        }
        if (!detail.empty())
        {
            message += ": " + detail;
        }
        return message;
    }

    bool operator==(const ModVersion& a, const ModVersion& b)
    {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
    }

    bool operator<(const ModVersion& a, const ModVersion& b)
    {
        if (a.major != b.major)
        {
            return a.major < b.major;
        }
        if (a.minor != b.minor)
        {
            return a.minor < b.minor;
        }
        return a.patch < b.patch;
    }

    Result<ModVersion, ModManifestError> parseModVersion(std::string_view text)
    {
        const auto invalid = [&]() {
            return fail(ModErrorCode::UnsupportedVersion, {},
                "version \"" + std::string(text)
                    + "\" is not major.minor.patch; format 1 does not accept pre-release or build metadata");
        };

        if (text.empty() || text.size() > 64)
        {
            return invalid();
        }
        std::array<std::uint32_t, 3> parts{};
        std::size_t part = 0;
        std::size_t start = 0;
        while (true)
        {
            const auto separator = text.find('.', start);
            const auto component = text.substr(start, separator == std::string_view::npos ? std::string_view::npos : separator - start);
            if (component.empty() || component.size() > 10)
            {
                return invalid();
            }
            if (component.size() > 1 && component[0] == '0')
            {
                return invalid();
            }
            std::uint64_t value = 0;
            for (const auto character : component)
            {
                if (character < '0' || character > '9')
                {
                    return invalid();
                }
                value = value * 10 + static_cast<std::uint64_t>(character - '0');
            }
            if (value > std::numeric_limits<std::uint32_t>::max())
            {
                return invalid();
            }
            if (part >= parts.size())
            {
                return invalid();
            }
            parts[part] = static_cast<std::uint32_t>(value);
            ++part;
            if (separator == std::string_view::npos)
            {
                break;
            }
            start = separator + 1;
        }
        if (part != 3)
        {
            return invalid();
        }
        return Ok(ModVersion{parts[0], parts[1], parts[2]});
    }

    std::string modVersionText(const ModVersion& version)
    {
        return std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
    }

    Result<bool, ModManifestError> satisfiesModConstraint(const ModVersion& version, std::string_view constraint)
    {
        const auto parsed = parseConstraint(std::string(constraint), {});
        if (!parsed)
        {
        return Err(parsed.getErr());
        }
        for (const auto& comparator : parsed.get())
        {
            if (!satisfiesComparator(version, comparator))
            {
            return Ok(false);
            }
        }
            return Ok(true);
    }

    bool isKnownResourceDirectory(const std::string& name)
    {
        static const std::set<std::string> names = {
            "ai", "anims", "bitmaps", "camps", "downloads", "features", "fonts", "gamedata",
            "guis", "builderGuis", "maps", "objects3d", "palettes", "scripts", "sounds",
            "textures", "unitpics", "units", "weapons"};
        return names.count(name) != 0;
    }

    std::string modKindText(ModKind kind)
    {
        switch (kind)
        {
        case ModKind::Mutator:
            return "mutator";
        case ModKind::Content:
            return "content";
        case ModKind::Ai:
            return "ai";
        case ModKind::Maps:
            return "maps";
        case ModKind::Presentation:
            return "presentation";
        case ModKind::Base:
            return "base";
        case ModKind::Assets:
            return "assets";
        }
        return "content";
    }

    std::optional<ModKind> modKindFromText(std::string_view text)
    {
        if (text == "mutator")
        {
            return ModKind::Mutator;
        }
        if (text == "content")
        {
            return ModKind::Content;
        }
        if (text == "ai")
        {
            return ModKind::Ai;
        }
        if (text == "maps")
        {
            return ModKind::Maps;
        }
        if (text == "presentation")
        {
            return ModKind::Presentation;
        }
        if (text == "base")
        {
            return ModKind::Base;
        }
        if (text == "assets")
        {
            return ModKind::Assets;
        }
        return std::nullopt;
    }

    Result<ModManifest, ModManifestError> parseModManifest(
        const json& value,
        const std::string& source,
        const std::string& implicitLegacyId)
    {
        if (!value.is_object())
        {
            return fail(ModErrorCode::InvalidManifest, {}, source + ": the manifest must be a JSON object");
        }

        ModManifest manifest;

        if (value.contains("manifestVersion"))
        {
            if (!value["manifestVersion"].is_number_integer())
            {
                return fail(ModErrorCode::InvalidField, {}, source + ": \"manifestVersion\" must be an integer");
            }
            const auto version = value["manifestVersion"].get<long long>();
            if (version < 1)
            {
                return fail(ModErrorCode::InvalidField, {}, source + ": \"manifestVersion\" must be at least 1");
            }
            if (version > currentModManifestVersion)
            {
                return fail(ModErrorCode::UnsupportedManifestVersion, {},
                    source + ": manifest format " + std::to_string(version) + " is newer than the supported "
                        + std::to_string(currentModManifestVersion));
            }
            manifest.manifestVersion = static_cast<int>(version);
        }
        else
        {
            manifest.legacy = true;
            manifest.manifestVersion = 0;
        }

        if (value.contains("id"))
        {
            if (!value["id"].is_string())
            {
                return fail(ModErrorCode::InvalidField, {}, source + ": \"id\" must be a string");
            }
            manifest.id = value["id"].get<std::string>();
            if (!validId(manifest.id))
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"id\" must match [a-z0-9][a-z0-9._-]* and be at most 128 characters");
            }
        }
        else if (manifest.legacy)
        {
            if (implicitLegacyId.empty())
            {
                return fail(ModErrorCode::MissingField, {},
                    source + ": a legacy descriptor has no \"id\"; supply an implicit content-derived id");
            }
            manifest.id = implicitLegacyId;
            if (!validId(manifest.id))
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": the implicit legacy id must match [a-z0-9][a-z0-9._-]*");
            }
        }
        else
        {
            return fail(ModErrorCode::MissingField, {}, source + ": \"id\" is required in a version 1 manifest");
        }

        if (value.contains("name"))
        {
            if (!value["name"].is_string())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"name\" must be a string");
            }
            manifest.name = value["name"].get<std::string>();
            if (manifest.name.size() > 200)
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"name\" must be at most 200 characters");
            }
        }
        if (manifest.name.empty())
        {
            manifest.name = manifest.id;
        }

        if (value.contains("version"))
        {
            if (!value["version"].is_string())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"version\" must be a string");
            }
            manifest.versionText = value["version"].get<std::string>();
            const auto parsed = parseModVersion(manifest.versionText);
            if (!parsed)
            {
                auto error = parsed.getErr();
                error.packageId = manifest.id;
                error.detail = source + ": " + error.detail;
            return Err(error);
            }
            manifest.version = parsed.get();
        }
        else if (manifest.legacy)
        {
            manifest.versionText = "0.0.0";
        }
        else
        {
            return fail(ModErrorCode::MissingField, manifest.id, source + ": \"version\" is required in a version 1 manifest");
        }

        if (value.contains("kind"))
        {
            if (!value["kind"].is_string())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"kind\" must be a string");
            }
            const auto kind = modKindFromText(value["kind"].get<std::string>());
            if (!kind)
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"kind\" is not a known package kind");
            }
            manifest.kind = *kind;
        }

        if (value.contains("api"))
        {
            if (!value["api"].is_number_integer())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"api\" must be an integer");
            }
            const auto api = value["api"].get<long long>();
            if (api < 1)
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"api\" must be at least 1");
            }
            manifest.api = static_cast<int>(api);
        }

        if (value.contains("entry"))
        {
            if (!value["entry"].is_string())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"entry\" must be a string");
            }
            const auto entry = value["entry"].get<std::string>();
            if (!validEntryPath(entry))
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"entry\" must be a normalized relative path with no parent traversal");
            }
            manifest.entry = entry;
        }
        if (manifest.kind == ModKind::Mutator)
        {
            if (!manifest.entry)
            {
                return fail(ModErrorCode::MissingField, manifest.id,
                    source + ": a mutator requires an \"entry\" script");
            }
            if (manifest.entry->size() < 4
                || manifest.entry->compare(manifest.entry->size() - 4, 4, ".lua") != 0)
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": format 1 mutator entry scripts must be .lua");
            }
        }

        if (value.contains("requires"))
        {
            if (!value["requires"].is_array())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"requires\" must be an array");
            }
            for (const auto& item : value["requires"])
            {
                if (!item.is_object())
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": each \"requires\" entry must be an object");
                }
                if (!item.contains("id") || !item["id"].is_string())
                {
                    return fail(ModErrorCode::MissingField, manifest.id,
                        source + ": each \"requires\" entry needs a string \"id\"");
                }
                ModDependency dependency;
                dependency.id = item["id"].get<std::string>();
                if (!validId(dependency.id))
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": requirement id \"" + dependency.id + "\" is not a valid package id");
                }
                if (dependency.id == manifest.id)
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": a package cannot require itself");
                }
                if (item.contains("version"))
                {
                    if (!item["version"].is_string())
                    {
                        return fail(ModErrorCode::InvalidField, manifest.id,
                            source + ": requirement \"" + dependency.id + "\" has a non-string version");
                    }
                    dependency.version = item["version"].get<std::string>();
                }
                const auto constraint = parseConstraint(dependency.version, manifest.id);
                if (!constraint)
                {
                    auto error = constraint.getErr();
                    error.detail = source + ": requirement \"" + dependency.id + "\": " + error.detail;
            return Err(error);
                }
                manifest.dependencies.push_back(std::move(dependency));
            }
        }

        if (auto error = readStringArray(value, "after", manifest.id, manifest.after, validId, "package ids"))
        {
            error->detail = source + ": " + error->detail;
            return Err(*error);
        }
        if (auto error = readStringArray(value, "before", manifest.id, manifest.before, validId, "package ids"))
        {
            error->detail = source + ": " + error->detail;
            return Err(*error);
        }
        for (const auto& reference : manifest.after)
        {
            if (reference == manifest.id)
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"after\" cannot name the package itself");
            }
        }
        for (const auto& reference : manifest.before)
        {
            if (reference == manifest.id)
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"before\" cannot name the package itself");
            }
        }

        if (auto error = readStringArray(value, "features", manifest.id, manifest.features, validFeatureName,
                "names of the form name@version"))
        {
            error->detail = source + ": " + error->detail;
            return Err(*error);
        }

        if (auto error = readStringArray(value, "exclusive", manifest.id, manifest.exclusive, validExclusiveKey,
                "lower-case keys"))
        {
            error->detail = source + ": " + error->detail;
            return Err(*error);
        }

        if (auto error = readStringArray(value, "replaces", manifest.id, manifest.replaces))
        {
            error->detail = source + ": " + error->detail;
            return Err(*error);
        }
        for (const auto& replacement : manifest.replaces)
        {
            const auto separator = replacement.find(':');
            if (separator == std::string::npos || separator == 0 || separator + 1 >= replacement.size())
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"replaces\" entry \"" + replacement + "\" must be \"<packageId>:<key>\"");
            }
            const auto owner = replacement.substr(0, separator);
            const auto key = replacement.substr(separator + 1);
            if (!validId(owner) || !validExclusiveKey(key))
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"replaces\" entry \"" + replacement + "\" names an invalid package id or key");
            }
            if (std::find(manifest.exclusive.begin(), manifest.exclusive.end(), key) == manifest.exclusive.end())
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"replaces\" names key \"" + key + "\", which this package does not claim");
            }
        }

        if (value.contains("resourceDirectories"))
        {
            if (!value["resourceDirectories"].is_object())
            {
                return fail(ModErrorCode::InvalidField, manifest.id,
                    source + ": \"resourceDirectories\" must be an object");
            }
            for (const auto& [name, directory] : value["resourceDirectories"].items())
            {
                if (!isKnownResourceDirectory(name))
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": unknown resource directory \"" + name + "\"");
                }
                if (!directory.is_string())
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": resource directory \"" + name + "\" must be a string");
                }
                const auto path = directory.get<std::string>();
                if (!validEntryPath(path))
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": resource directory \"" + name + "\" is not a normalized relative path");
                }
                manifest.resourceDirectories[name] = path;
            }
        }

        if (value.contains("options"))
        {
            if (!value["options"].is_array())
            {
                return fail(ModErrorCode::InvalidField, manifest.id, source + ": \"options\" must be an array");
            }
            std::set<std::string> seen;
            for (const auto& item : value["options"])
            {
                if (!item.is_object())
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": each \"options\" entry must be an object");
                }
                ModOption option;
                if (!item.contains("key") || !item["key"].is_string())
                {
                    return fail(ModErrorCode::MissingField, manifest.id,
                        source + ": each option needs a string \"key\"");
                }
                option.key = item["key"].get<std::string>();
                if (!validOptionKey(option.key))
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": option key \"" + option.key + "\" must match [a-z][a-z0-9_.]*");
                }
                if (!seen.insert(option.key).second)
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": option \"" + option.key + "\" is declared twice");
                }
                if (!item.contains("type") || !item["type"].is_string())
                {
                    return fail(ModErrorCode::MissingField, manifest.id,
                        source + ": option \"" + option.key + "\" needs a string \"type\"");
                }
                const auto type = item["type"].get<std::string>();
                if (type == "number")
                {
                    option.type = ModOptionType::Number;
                }
                else if (type == "boolean")
                {
                    option.type = ModOptionType::Boolean;
                }
                else if (type == "string")
                {
                    option.type = ModOptionType::String;
                }
                else
                {
                    return fail(ModErrorCode::InvalidField, manifest.id,
                        source + ": option \"" + option.key + "\" has unknown type \"" + type + "\"");
                }

                if (option.type == ModOptionType::Number)
                {
                    if (item.contains("min"))
                    {
                        if (!item["min"].is_number())
                        {
                            return fail(ModErrorCode::InvalidField, manifest.id,
                                source + ": option \"" + option.key + "\" has a non-number \"min\"");
                        }
                        option.minimum = item["min"].get<double>();
                    }
                    if (item.contains("max"))
                    {
                        if (!item["max"].is_number())
                        {
                            return fail(ModErrorCode::InvalidField, manifest.id,
                                source + ": option \"" + option.key + "\" has a non-number \"max\"");
                        }
                        option.maximum = item["max"].get<double>();
                    }
                    if (option.minimum && option.maximum && *option.minimum > *option.maximum)
                    {
                        return fail(ModErrorCode::InvalidField, manifest.id,
                            source + ": option \"" + option.key + "\" has min greater than max");
                    }
                }
                if (item.contains("choices"))
                {
                    if (option.type != ModOptionType::String)
                    {
                        return fail(ModErrorCode::InvalidField, manifest.id,
                            source + ": option \"" + option.key + "\" has \"choices\" but is not a string");
                    }
                    if (!item["choices"].is_array() || item["choices"].empty())
                    {
                        return fail(ModErrorCode::InvalidField, manifest.id,
                            source + ": option \"" + option.key + "\" needs a non-empty \"choices\" array");
                    }
                    std::set<std::string> chosen;
                    for (const auto& choice : item["choices"])
                    {
                        if (!choice.is_string() || choice.get<std::string>().empty())
                        {
                            return fail(ModErrorCode::InvalidField, manifest.id,
                                source + ": option \"" + option.key + "\" has a non-string choice");
                        }
                        const auto text = choice.get<std::string>();
                        if (!chosen.insert(text).second)
                        {
                            return fail(ModErrorCode::InvalidField, manifest.id,
                                source + ": option \"" + option.key + "\" repeats choice \"" + text + "\"");
                        }
                        option.choices.push_back(text);
                    }
                }

                if (item.contains("default"))
                {
                    option.defaultValue = item["default"];
                    option.hasDefault = true;
                    if (auto error = validateOptionValue(option, option.defaultValue, manifest.id))
                    {
                        error->detail = source + ": default for " + error->detail;
            return Err(*error);
                    }
                }
                manifest.options.push_back(std::move(option));
            }
        }

        return Ok(manifest);
    }

    Result<ModPackage, ModManifestError> modPackageFromJson(
        const json& value,
        const std::string& rawManifestText,
        const std::string& source,
        const std::string& implicitLegacyId)
    {
        const auto manifest = parseModManifest(value, source, implicitLegacyId);
        if (!manifest)
        {
            return Err(manifest.getErr());
        }
        // entrySha256 starts empty: the caller reads the entry script and sets
        // it (MOD-SDK-014). Named here because MinGW's -Werror build refuses
        // a missing field initialiser.
        return Ok(ModPackage{manifest.get(), sha256Hex(rawManifestText), source, {}});
    }


    bool isModSha256Hex(std::string_view text)
    {
        return text.size() == 64 && text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
    }

    std::string canonicalJson(const json& value)
    {
        return value.dump(-1, ' ', false, json::error_handler_t::strict);
    }

    json resolvedModSetJson(const ResolvedModSet& set)
    {
        json root;
        root["format"] = "rwe.resolved-mods";
        root["formatVersion"] = set.formatVersion;
        root["api"] = set.apiVersion;

        json packages = json::array();
        for (const auto& mod : set.mods)
        {
            json package;
            package["id"] = mod.manifest.id;
            package["version"] = mod.manifest.versionText;
            package["kind"] = modKindText(mod.manifest.kind);
            package["api"] = mod.manifest.api;
            package["entry"] = mod.manifest.entry ? json(*mod.manifest.entry) : json(nullptr);
            package["manifestSha256"] = mod.manifestSha256;
            package["entrySha256"] = mod.entrySha256.empty() ? json(nullptr) : json(mod.entrySha256);

            auto features = mod.manifest.features;
            std::sort(features.begin(), features.end());
            package["features"] = features;

            auto exclusive = mod.ownedExclusiveKeys;
            std::sort(exclusive.begin(), exclusive.end());
            package["exclusive"] = exclusive;

            json options = json::object();
            for (const auto& [key, value] : mod.options)
            {
                options[key] = value;
            }
            package["options"] = std::move(options);

            json directories = json::object();
            for (const auto& [name, directory] : mod.manifest.resourceDirectories)
            {
                directories[name] = directory;
            }
            package["resourceDirectories"] = std::move(directories);

            packages.push_back(std::move(package));
        }
        root["packages"] = std::move(packages);
        return root;
    }

    Result<ResolvedModSet, ModManifestError> resolveModSet(
        std::vector<ModPackage> packages,
        const ModOptionSelections& selectedOptions,
        const ModResolutionEnvironment& environment)
    {
        std::map<std::string, std::size_t> byId;
        for (std::size_t i = 0; i < packages.size(); ++i)
        {
            const auto& package = packages[i];
            if (!byId.emplace(package.manifest.id, i).second)
            {
                return fail(ModErrorCode::DuplicateId, package.manifest.id,
                    "two packages in the set share this id; give one a different stable id");
            }
            if (package.manifestSha256.size() != 64
                || package.manifestSha256.find_first_not_of("0123456789abcdef") != std::string::npos)
            {
                return fail(ModErrorCode::InvalidField, package.manifest.id,
                    "manifestSha256 must be 64 lowercase hex characters; build the package with modPackageFromJson");
            }
            // MOD-SDK-013 keeps presentation scripts out of the identity: what
            // a HUD draws never reaches the simulation, and +ReloadPresentation
            // exists to change it mid-game. Only simulation scripts are hashed.
            const auto hashesEntry = package.manifest.entry && package.manifest.kind != ModKind::Presentation;
            if (hashesEntry && !isModSha256Hex(package.entrySha256))
            {
                return fail(ModErrorCode::MissingField, package.manifest.id,
                    "entry \"" + *package.manifest.entry
                        + "\" has no content hash; read the script and set entrySha256 to its SHA-256");
            }
            if (!hashesEntry && !package.entrySha256.empty())
            {
                return fail(ModErrorCode::InvalidField, package.manifest.id,
                    package.manifest.entry
                        ? "entrySha256 is set on a presentation package, whose script is not part of the identity"
                        : "entrySha256 is set but the manifest names no entry script");
            }
            if (package.manifest.api != environment.apiVersion)
            {
                return fail(ModErrorCode::UnsupportedApi, package.manifest.id,
                    "requires authoring API " + std::to_string(package.manifest.api) + ", this build supports "
                        + std::to_string(environment.apiVersion));
            }
            if (std::find(environment.supportedKinds.begin(), environment.supportedKinds.end(), package.manifest.kind)
                == environment.supportedKinds.end())
            {
                return fail(ModErrorCode::UnsupportedKind, package.manifest.id,
                    "kind \"" + modKindText(package.manifest.kind) + "\" is not supported by this build");
            }
            for (const auto& feature : package.manifest.features)
            {
                if (std::find(environment.supportedFeatures.begin(), environment.supportedFeatures.end(), feature)
                    == environment.supportedFeatures.end())
                {
                    return fail(ModErrorCode::UnsupportedFeature, package.manifest.id,
                        "requires feature \"" + feature + "\", which this build does not provide");
                }
            }
        }

        for (const auto& [id, selection] : selectedOptions)
        {
            (void)selection;
            if (!byId.count(id))
            {
                return fail(ModErrorCode::UnknownPackage, id,
                    "options were supplied for a package that is not in the resolved set");
            }
        }

        std::map<std::string, std::set<std::string>> successors;
        std::map<std::string, int> indegree;
        for (const auto& package : packages)
        {
            indegree[package.manifest.id] = 0;
        }
        const auto addEdge = [&](const std::string& before, const std::string& after) {
            if (successors[before].insert(after).second)
            {
                ++indegree[after];
            }
        };

        for (const auto& package : packages)
        {
            for (const auto& dependency : package.manifest.dependencies)
            {
                if (!byId.count(dependency.id))
                {
                    return fail(ModErrorCode::MissingDependency, package.manifest.id,
                        "requires \"" + dependency.id + "\", which is not in the set");
                }
                const auto& target = packages[byId.at(dependency.id)].manifest;
                const auto satisfied = satisfiesModConstraint(target.version, dependency.version);
                if (!satisfied)
                {
                    return Err(satisfied.getErr());
                }
                if (!satisfied.get())
                {
                    return fail(ModErrorCode::UnsatisfiedDependency, package.manifest.id,
                        "requires \"" + dependency.id + "\" " + dependency.version + ", but the set has "
                            + target.versionText);
                }
                addEdge(dependency.id, package.manifest.id);
            }
            for (const auto& reference : package.manifest.after)
            {
                if (!byId.count(reference))
                {
                    return fail(ModErrorCode::UnknownOrderingReference, package.manifest.id,
                        "\"after\" names \"" + reference + "\", which is not in the set");
                }
                addEdge(reference, package.manifest.id);
            }
            for (const auto& reference : package.manifest.before)
            {
                if (!byId.count(reference))
                {
                    return fail(ModErrorCode::UnknownOrderingReference, package.manifest.id,
                        "\"before\" names \"" + reference + "\", which is not in the set");
                }
                addEdge(package.manifest.id, reference);
            }
        }

        std::set<std::string> ready;
        for (const auto& [id, degree] : indegree)
        {
            if (degree == 0)
            {
                ready.insert(id);
            }
        }
        std::vector<std::string> order;
        while (!ready.empty())
        {
            const auto id = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(id);
            for (const auto& next : successors[id])
            {
                if (--indegree[next] == 0)
                {
                    ready.insert(next);
                }
            }
        }
        if (order.size() != packages.size())
        {
            std::string remaining;
            for (const auto& [id, degree] : indegree)
            {
                if (degree > 0)
                {
                    remaining += (remaining.empty() ? "" : ", ") + id;
                }
            }
            return fail(ModErrorCode::DependencyCycle, {},
                "ordering constraints form a cycle among: " + remaining);
        }

        ResolvedModSet set;
        set.apiVersion = environment.apiVersion;
        for (const auto& id : order)
        {
            const auto& package = packages[byId.at(id)];
            ResolvedMod mod;
            mod.manifest = package.manifest;
            mod.manifestSha256 = package.manifestSha256;
            mod.entrySha256 = package.entrySha256;

            const auto selection = selectedOptions.find(id);
            for (const auto& option : package.manifest.options)
            {
                std::optional<json> chosen;
                if (selection != selectedOptions.end())
                {
                    const auto found = selection->second.find(option.key);
                    if (found != selection->second.end())
                    {
                        chosen = found->second;
                    }
                }
                if (!chosen && option.hasDefault)
                {
                    chosen = option.defaultValue;
                }
                if (!chosen)
                {
                    return fail(ModErrorCode::MissingOptionValue, id,
                        "option \"" + option.key + "\" has no default and was not supplied");
                }
                if (auto error = validateOptionValue(option, *chosen, id))
                {
            return Err(*error);
                }
                mod.options.emplace(option.key, *chosen);
            }
            if (selection != selectedOptions.end())
            {
                for (const auto& [key, value] : selection->second)
                {
                    (void)value;
                    if (mod.options.find(key) == mod.options.end())
                    {
                        return fail(ModErrorCode::UnknownOption, id,
                            "option \"" + key + "\" is not declared by this package");
                    }
                }
            }
            set.mods.push_back(std::move(mod));
        }

        std::map<std::string, std::string> keyOwner;
        for (const auto& mod : set.mods)
        {
            for (const auto& key : mod.manifest.exclusive)
            {
                const auto existing = keyOwner.find(key);
                if (existing == keyOwner.end())
                {
                    keyOwner.emplace(key, mod.manifest.id);
                    continue;
                }
                // ADR-0002 D5: `replaces = <mod>:<key>` names the owner
                // whose key is being taken over, not the taker.
                const auto replacement = existing->second + ":" + key;
                if (std::find(mod.manifest.replaces.begin(), mod.manifest.replaces.end(), replacement)
                    != mod.manifest.replaces.end())
                {
                    existing->second = mod.manifest.id;
                    continue;
                }
                return fail(ModErrorCode::ExclusiveConflict, mod.manifest.id,
                    "\"" + mod.manifest.id + "\" and \"" + existing->second + "\" both claim \"" + key
                        + "\"; declare \"replaces\": [\"" + existing->second + ":" + key + "\"] or remove one");
            }
        }
        for (const auto& [key, owner] : keyOwner)
        {
            for (auto& mod : set.mods)
            {
                if (mod.manifest.id == owner)
                {
                    mod.ownedExclusiveKeys.push_back(key);
                    break;
                }
            }
        }
        for (auto& mod : set.mods)
        {
            std::sort(mod.ownedExclusiveKeys.begin(), mod.ownedExclusiveKeys.end());
        }

        set.identity = sha256Hex(canonicalJson(resolvedModSetJson(set)));
        return Ok(set);
    }
}
