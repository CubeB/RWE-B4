#include "SimEventLog.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/SimpleLogger.h>
#include <type_traits>
#include <variant>
#include <vector>

namespace rwe
{
    /**
     * Everything the header would otherwise have made every translation unit
     * pay for: the value variant, the field and event records, and the buffer.
     */
    struct SimEventLog::Impl
    {
        using Value = std::variant<
            std::monostate,
            bool,
            int,
            unsigned int,
            long,
            unsigned long,
            long long,
            unsigned long long,
            float,
            double,
            std::string,
            std::map<std::string, int>>;

        struct Field
        {
            std::string key;
            Value value;
        };

        struct Pending
        {
            unsigned int tick;
            std::string ev;
            std::vector<Field> fields;
        };

        std::vector<Pending> events;

        /**
         * A template rather than an overload set, and here rather than in the
         * header, so the conversion to Value is instantiated in this file
         * alone.
         */
        template <typename T>
        void addField(std::size_t index, const std::string& key, T&& value)
        {
            if (index == NotRecording)
            {
                return;
            }

            events[index].fields.push_back(Field{key, Value(std::forward<T>(value))});
        }
    };

    SimEventLog::SimEventLog()
        : impl(std::make_unique<Impl>())
    {
    }

    // Out of line, all four, because Impl is incomplete where the class is
    // declared and these are the ones that would need it there.
    SimEventLog::~SimEventLog() = default;
    SimEventLog::SimEventLog(SimEventLog&&) noexcept = default;
    SimEventLog& SimEventLog::operator=(SimEventLog&&) noexcept = default;

    SimEventLog::Event::Event(const SimEventLog* log, std::size_t index)
        : log(log), index(index)
    {
    }

    SimEventLog::Event SimEventLog::event(unsigned int tick, const std::string& name) const
    {
        if (!recording)
        {
            return Event(this, NotRecording);
        }

        impl->events.push_back(Impl::Pending{tick, name, {}});
        return Event(this, impl->events.size() - 1);
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, bool value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, int value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, unsigned int value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, long value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, unsigned long value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, long long value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, unsigned long long value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, float value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, double value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, const std::string& value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, const char* value)
    {
        log->impl->addField(index, key, std::string(value));
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, const std::map<std::string, int>& value)
    {
        log->impl->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, std::nullptr_t)
    {
        log->impl->addField(index, key, std::monostate{});
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::detail(const std::string& text)
    {
        return set("detail", text);
    }

    void SimEventLog::write(const std::filesystem::path& path) const
    {
        std::ofstream out(path);
        if (!out)
        {
            LOG_ERROR << "AI arena: could not write " << path.string();
            return;
        }

        for (const auto& pending : impl->events)
        {
            nlohmann::json j;
            j["schema"] = 1;
            j["tick"] = pending.tick;
            j["secs"] = static_cast<double>(pending.tick) / static_cast<double>(SimTicksPerSecond);
            j["ev"] = pending.ev;
            for (const auto& field : pending.fields)
            {
                j[field.key] = std::visit(
                    [](const auto& v) -> nlohmann::json {
                        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, std::monostate>)
                        {
                            return nullptr;
                        }
                        else
                        {
                            return nlohmann::json(v);
                        }
                    },
                    field.value);
            }
            out << j.dump() << '\n';
        }
    }

    void SimEventLog::clear() const
    {
        impl->events.clear();
    }

    bool SimEventLog::empty() const
    {
        return impl->events.empty();
    }
}
