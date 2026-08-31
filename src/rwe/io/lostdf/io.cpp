#include "io.h"
#include <rwe/util/rwe_string.h>

namespace rwe
{
    namespace
    {
        std::vector<int> parseIntList(const std::string& value)
        {
            std::vector<int> numbers;
            for (const auto& part : split(value, ','))
            {
                // stringstream extraction skips leading whitespace, so only a
                // wholly blank field needs dropping (trailing separators).
                if (part.find_first_not_of(" \t\r\n") == std::string::npos)
                {
                    continue;
                }

                auto parsed = tdfTryParse<int>(part);
                if (!parsed)
                {
                    throw TdfValueException("Failed to parse line of sight offset: " + part);
                }

                numbers.push_back(*parsed);
            }

            return numbers;
        }

        LosRay parseLosLine(const std::string& value)
        {
            auto numbers = parseIntList(value);
            if (numbers.empty())
            {
                throw TdfValueException("Line of sight ray is empty");
            }

            auto stepCount = numbers[0];
            if (stepCount < 0 || static_cast<std::size_t>(stepCount) * 2u + 1u != numbers.size())
            {
                throw TdfValueException("Line of sight ray step count does not match its offsets");
            }

            LosRay ray;
            ray.reserve(stepCount);
            for (int i = 0; i < stepCount; ++i)
            {
                ray.emplace_back(numbers[(i * 2) + 1], numbers[(i * 2) + 2]);
            }

            return ray;
        }
    }

    LosTables parseLosTdf(const TdfBlock& root)
    {
        auto tableInfo = root.findBlock("TABLEINFO");
        if (!tableInfo)
        {
            throw TdfValueException("los.tdf has no TABLEINFO block");
        }

        auto tableCount = static_cast<int>(tableInfo->get().expectUint("numtables"));
        if (tableCount < 1)
        {
            throw TdfValueException("los.tdf declares no tables");
        }

        LosTables result;
        result.tables.resize(tableCount);

        // TABLE_r holds the fan for radius r, so radii run 1..numtables-1.
        // Entry 0 stays empty.
        for (int r = 1; r < tableCount; ++r)
        {
            auto blockName = "TABLE" + std::to_string(r);
            auto block = root.findBlock(blockName);
            if (!block)
            {
                throw TdfValueException("los.tdf is missing " + blockName);
            }

            auto lineCount = static_cast<int>(block->get().expectUint("numlines"));
            auto& table = result.tables[r];
            table.rays.reserve(lineCount);
            for (int i = 1; i <= lineCount; ++i)
            {
                auto key = "line" + std::to_string(i);
                auto value = block->get().findValue(key);
                if (!value)
                {
                    throw TdfValueException("los.tdf " + blockName + " is missing " + key);
                }

                table.rays.push_back(parseLosLine(value->get()));
            }
        }

        return result;
    }
}
