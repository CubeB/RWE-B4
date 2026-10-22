#include "SoundClass.h"
#include <rwe/io/tdf/SimpleTdfAdapter.h>

namespace rwe
{
    SoundClass parseSoundClass(const TdfBlock& block)
    {
        SoundClass s;

        s.select1 = block.findValue("select1");
        s.unitComplete = block.findValue("unitcomplete");
        s.activate = block.findValue("activate");
        s.deactivate = block.findValue("deactivate");
        s.ok1 = block.findValue("ok1");
        s.arrived1 = block.findValue("arrived1");
        s.cant1 = block.findValue("cant1");
        s.underAttack = block.findValue("underattack");
        s.build = block.findValue("build");
        s.repair = block.findValue("repair");
        s.working = block.findValue("working");
        s.cloak = block.findValue("cloak");
        s.uncloak = block.findValue("uncloak");
        s.capture = block.findValue("capture");
        s.count5 = block.findValue("count5");
        s.count4 = block.findValue("count4");
        s.count3 = block.findValue("count3");
        s.count2 = block.findValue("count2");
        s.count1 = block.findValue("count1");
        s.cancelDestruct = block.findValue("canceldestruct");

        return s;
    }

    namespace
    {
        // BT-133: numeric category bindings use SOUND.TDF's file order.
        class SoundTdfAdapter : public TdfAdapter<std::vector<std::pair<std::string, SoundClass>>>
        {
            SimpleTdfAdapter blocks;
            std::vector<std::string> names;
            unsigned int depth{0};

        public:
            void onStart() override
            {
                blocks.onStart();
            }

            void onProperty(const std::string& name, const std::string& value) override
            {
                blocks.onProperty(name, value);
            }

            void onStartBlock(const std::string& name) override
            {
                if (depth++ == 0)
                {
                    names.push_back(name);
                }
                blocks.onStartBlock(name);
            }

            void onEndBlock() override
            {
                blocks.onEndBlock();
                --depth;
            }

            Result onDone() override
            {
                auto root = blocks.onDone();
                Result result;
                result.reserve(names.size());
                for (const auto& name : names)
                {
                    result.emplace_back(name, parseSoundClass(root.findBlock(name)->get()));
                }
                return result;
            }
        };
    }

    std::vector<std::pair<std::string, SoundClass>> parseSoundTdf(const std::string& input)
    {
        TdfParser<ConstUtf8UncheckedIterator, std::vector<std::pair<std::string, SoundClass>>> parser(new SoundTdfAdapter);
        auto text = utf8::is_valid(input.begin(), input.end()) ? input : latin1ToUtf8(input);
        return parser.parse(cUtf8UncheckedBegin(text), cUtf8UncheckedEnd(text));
    }
}
