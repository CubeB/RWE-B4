#include "SimEventLog.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
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

        events.push_back(Pending{tick, name, {}});
        return Event(this, events.size() - 1);
    }

    void SimEventLog::addField(std::size_t index, const std::string& key, Value value) const
    {
        if (index == NotRecording)
        {
            return;
        }

        events[index].fields.push_back(Field{key, std::move(value)});
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, bool value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, int value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, unsigned int value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, long value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, unsigned long value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, long long value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, unsigned long long value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, float value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, double value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, const std::string& value)
    {
        log->addField(index, key, value);
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, const char* value)
    {
        log->addField(index, key, std::string(value));
        return *this;
    }

    SimEventLog::Event& SimEventLog::Event::set(const std::string& key, const std::map<std::string, int>& value)
    {
        log->addField(index, key, value);
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

        for (const auto& pending : events)
        {
            nlohmann::json j;
            j["schema"] = 1;
            j["tick"] = pending.tick;
            j["secs"] = static_cast<double>(pending.tick) / static_cast<double>(SimTicksPerSecond);
            j["ev"] = pending.ev;
            for (const auto& field : pending.fields)
            {
                j[field.key] = std::visit([](const auto& v) { return nlohmann::json(v); }, field.value);
            }
            out << j.dump() << '\n';
        }
    }

    void SimEventLog::clear() const
    {
        events.clear();
    }
}
