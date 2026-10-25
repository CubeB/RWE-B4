#include "ClassicAiScript.h"

#include <algorithm>
#include <cstdint>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/util/rwe_string.h>
#include <map>

namespace rwe
{
    ClassicDownloadableScript parseClassicDownloadableScript(const TdfBlock& unitInfo)
    {
        ClassicDownloadableScript result;
        result.unitName = toUpper(unitInfo.expectString("UnitName"));
        if (const auto text = unitInfo.findValue("ai_weight"))
        {
            result.text = text->get().substr(0, 63);
        }
        result.downloadable = (unitInfo.extractInt("Downloadable").value_or(0) & 1) != 0;
        return result;
    }

    void promoteDownloadableAiScript(std::vector<ClassicDownloadableScript>& scripts, const std::vector<TdfBlock>& blocks, const std::set<std::string>& loadedTypes)
    {
        // DOWNLOAD-LINK-01: only a valid stored first row can promote a type.
        if (blocks.empty())
        {
            return;
        }
        const auto builder = blocks.front().findValue("UNITMENU");
        const auto product = blocks.front().findValue("UNITNAME");
        if (!builder || !product || loadedTypes.count(toUpper(builder->get())) == 0 || loadedTypes.count(toUpper(product->get())) == 0)
        {
            return;
        }
        for (auto& script : scripts)
        {
            if (script.unitName == toUpper(product->get()))
            {
                script.downloadable = true;
            }
        }
    }

    namespace
    {
        char upper(char c)
        {
            return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        }

        bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
        }

        std::string stripComment(const std::string& line)
        {
            auto end = line.size();
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '#' || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
                {
                    end = i;
                    break;
                }
            }
            return line.substr(0, end);
        }

        std::vector<std::string> tokens(const std::string& line)
        {
            std::vector<std::string> out;
            std::string current;
            for (char c : line)
            {
                if (isSpace(c) || c == ';')
                {
                    if (!current.empty())
                    {
                        out.push_back(current);
                        current.clear();
                    }
                }
                else
                {
                    current.push_back(upper(c));
                }
            }
            if (!current.empty())
            {
                out.push_back(current);
            }
            return out;
        }

        bool categoryHas(const std::string& categoryList, const std::string& token)
        {
            if (token.empty())
            {
                return false;
            }
            // The category registrar (0x488E70) always sets a definition's bit
            // in the category named ALL, whether or not its FBI listed it, so
            // every type is in it (BT-78).
            if (token == "ALL")
            {
                return true;
            }
            std::size_t i = 0;
            while (i < categoryList.size())
            {
                while (i < categoryList.size() && isSpace(categoryList[i]))
                {
                    ++i;
                }
                std::size_t start = i;
                while (i < categoryList.size() && !isSpace(categoryList[i]))
                {
                    ++i;
                }
                if (i > start && categoryList.compare(start, i - start, token) == 0 && i - start == token.size())
                {
                    return true;
                }
            }
            return false;
        }

        bool matchesPlan(const std::vector<std::string>& args, ClassicPlan plan)
        {
            const char* name = plan == ClassicPlan::Easy ? "EASY" : plan == ClassicPlan::Medium ? "MEDIUM" : "HARD";
            // Difficulty names are compared at every argument, but the `any`
            // comparison always reads argument 1 (the index pushed at 0x406C90
            // is constant), so `any` counts only as the first word (BT-128).
            if (!args.empty() && args.front() == "ANY")
            {
                return true;
            }
            for (const auto& a : args)
            {
                if (a == name)
                {
                    return true;
                }
            }
            return false;
        }

        int scaleMultiplier(int existing, double factor)
        {
            double scaled = static_cast<double>(existing) * factor;
            if (!(scaled > 0.0))
            {
                return 0;
            }
            if (scaled >= 100.0)
            {
                return 100;
            }
            return static_cast<int>(scaled);
        }

        int limitFrom(double value)
        {
            // The setter (0x409E90) stores the integer unchanged: -1 is
            // unlimited and any other negative stays negative, which fails
            // the strictly-below test later and blocks the type (BT-128).
            if (!(value > -1000000.0))
            {
                return -1000000;
            }
            if (value > 1000000.0)
            {
                return 1000000;
            }
            return static_cast<int>(value);
        }
    }

    std::vector<ClassicScriptDirective> parseClassicScript(const std::string& text)
    {
        std::vector<ClassicScriptDirective> out;
        std::size_t lineStart = 0;
        while (lineStart <= text.size())
        {
            auto lineEnd = text.find('\n', lineStart);
            if (lineEnd == std::string::npos)
            {
                lineEnd = text.size();
            }
            auto words = tokens(stripComment(text.substr(lineStart, lineEnd - lineStart)));
            if (!words.empty())
            {
                ClassicScriptDirective d;
                bool known = true;
                if (words[0] == "PLAN")
                {
                    d.kind = ClassicScriptDirective::Kind::Plan;
                }
                else if (words[0] == "WEIGHT")
                {
                    d.kind = ClassicScriptDirective::Kind::Weight;
                }
                else if (words[0] == "LIMIT")
                {
                    d.kind = ClassicScriptDirective::Kind::Limit;
                }
                else
                {
                    known = false;
                }
                if (known)
                {
                    d.args.assign(words.begin() + 1, words.end());
                    out.push_back(std::move(d));
                }
            }
            if (lineEnd == text.size())
            {
                break;
            }
            lineStart = lineEnd + 1;
        }
        return out;
    }

    double classicParseNumber(const std::string& text)
    {
        std::size_t i = 0;
        while (i < text.size() && isSpace(text[i]))
        {
            ++i;
        }
        bool negative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        {
            negative = text[i] == '-';
            ++i;
        }

        // Up to 15 significant digits go into an exactly representable integer,
        // and a power of ten up to 10^22 is exactly representable too, so one
        // IEEE division is the correctly rounded value of the decimal.
        std::uint64_t mantissa = 0;
        int digits = 0;
        int exponent = 0;
        bool any = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        {
            any = true;
            if (digits < 15)
            {
                mantissa = mantissa * 10u + static_cast<std::uint64_t>(text[i] - '0');
                if (mantissa != 0)
                {
                    ++digits;
                }
            }
            else
            {
                ++exponent;
            }
            ++i;
        }
        if (i < text.size() && text[i] == '.')
        {
            ++i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9')
            {
                any = true;
                if (digits < 15)
                {
                    mantissa = mantissa * 10u + static_cast<std::uint64_t>(text[i] - '0');
                    if (mantissa != 0)
                    {
                        ++digits;
                    }
                    --exponent;
                }
                ++i;
            }
        }
        if (!any)
        {
            return 0.0;
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
        {
            std::size_t j = i + 1;
            bool expNegative = false;
            if (j < text.size() && (text[j] == '+' || text[j] == '-'))
            {
                expNegative = text[j] == '-';
                ++j;
            }
            if (j < text.size() && text[j] >= '0' && text[j] <= '9')
            {
                int e = 0;
                while (j < text.size() && text[j] >= '0' && text[j] <= '9')
                {
                    if (e < 1000)
                    {
                        e = e * 10 + (text[j] - '0');
                    }
                    ++j;
                }
                exponent += expNegative ? -e : e;
            }
        }

        double value = static_cast<double>(mantissa);
        if (mantissa != 0)
        {
            while (exponent > 0 && value < 1e300)
            {
                value *= 10.0;
                --exponent;
            }
            if (exponent < 0)
            {
                // The common case: a short decimal, one exact division.
                int shift = -exponent;
                double divisor = 1.0;
                while (shift > 0 && divisor < 1e22)
                {
                    divisor *= 10.0;
                    --shift;
                }
                value /= divisor;
                while (shift > 0 && value > 1e-300)
                {
                    value /= 10.0;
                    --shift;
                }
            }
        }
        return negative ? -value : value;
    }

    ClassicScriptReport applyClassicScript(
        const std::vector<ClassicScriptDirective>& directives,
        ClassicPlan plan,
        const std::vector<ClassicScriptType>& types,
        std::vector<ClassicTypeEntry>& entries,
        bool* commandGate)
    {
        ClassicScriptReport report;
        bool localGate = true;
        bool& enabled = commandGate ? *commandGate : localGate;

        for (const auto& d : directives)
        {
            if (d.kind == ClassicScriptDirective::Kind::Plan)
            {
                // S:123: first switches both directives off, then back on for a match.
                enabled = false;
                if (matchesPlan(d.args, plan))
                {
                    enabled = true;
                }
                continue;
            }
            if (!enabled || d.args.empty())
            {
                continue;
            }

            // A line with a subject and no number reads the accessor's default,
            // zero (0x4B7410, 0x4B73E0): `Limit ARMSABO` forbids the type and
            // a bare weight zeroes it (BT-128).
            const auto& subject = d.args[0];
            const double number = d.args.size() >= 2 ? classicParseNumber(d.args[1]) : 0.0;

            // A name is a one-type set and locks; a category is a mask and does not.
            std::vector<std::size_t> hit;
            bool locks = false;
            for (std::size_t i = 0; i < types.size(); ++i)
            {
                if (types[i].name == subject)
                {
                    hit.push_back(i);
                    locks = true;
                    break;
                }
            }
            if (hit.empty())
            {
                for (std::size_t i = 0; i < types.size(); ++i)
                {
                    if (categoryHas(types[i].category, subject))
                    {
                        hit.push_back(i);
                    }
                }
            }
            if (hit.empty())
            {
                if (std::find(report.unknownNames.begin(), report.unknownNames.end(), subject) == report.unknownNames.end())
                {
                    report.unknownNames.push_back(subject);
                }
                continue;
            }

            for (auto i : hit)
            {
                auto& e = entries[i];
                if (d.kind == ClassicScriptDirective::Kind::Weight)
                {
                    if (!e.multiplierLocked)
                    {
                        e.multiplier = scaleMultiplier(e.multiplier, number);
                        e.multiplierLocked = locks;
                    }
                }
                else
                {
                    if (!e.limitLocked)
                    {
                        e.limit = limitFrom(number);
                        e.limitLocked = locks;
                    }
                }
            }
        }
        return report;
    }

    ClassicScriptReport applyClassicDownloadableScripts(
        const std::vector<ClassicDownloadableScript>& scripts, ClassicPlan plan,
        const std::vector<ClassicScriptType>& types, std::vector<ClassicTypeEntry>& entries)
    {
        std::map<std::string, const ClassicDownloadableScript*> byName;
        for (const auto& script : scripts)
        {
            if (script.downloadable && !script.text.empty())
            {
                byName.emplace(toUpper(script.unitName), &script);
            }
        }
        std::map<std::string, std::size_t> owners;
        for (std::size_t i = 0; i < types.size(); ++i)
        {
            owners.emplace(toUpper(types[i].name), i);
        }
        ClassicScriptReport report;
        for (int pass = 0; pass < 2; ++pass)
        {
            bool enabled = true;
            for (const auto& [name, index] : owners)
            {
                const auto script = byName.find(name);
                if (script == byName.end() || (pass == 0 ? entries[index].multiplierLocked : entries[index].limitLocked))
                {
                    continue;
                }
                auto fragment = applyClassicScript(parseClassicScript(script->second->text), plan, types, entries, &enabled);
                for (const auto& unknown : fragment.unknownNames)
                {
                    if (std::find(report.unknownNames.begin(), report.unknownNames.end(), unknown) == report.unknownNames.end())
                    {
                        report.unknownNames.push_back(unknown);
                    }
                }
            }
        }
        return report;
    }
}
