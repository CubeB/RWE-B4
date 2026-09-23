#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace rwe
{
    /**
     * The structured event sink behind `event-log.jsonl` (design §9).
     *
     * A pure observer, like AiArenaReport: events are buffered in memory as
     * the simulation runs and written out only when the report is flushed, so
     * nothing here touches the tick's I/O, the sync hash, the save or the
     * state dump, and nothing here can change what the simulation does.
     *
     * An event carries the sim tick and derived sim seconds and nothing
     * measured by the wall clock, so the same seed produces a byte-identical
     * log and the desync checker can diff two runs' logs as well as their
     * CSVs.
     *
     * The builder stores its values type-erased so this header does not drag
     * nlohmann into every AI translation unit; the JSON is assembled at flush
     * time in the .cpp.
     */
    class SimEventLog
    {
    public:
        class Event
        {
        public:
            Event& set(const std::string& key, bool value);
            Event& set(const std::string& key, int value);
            Event& set(const std::string& key, unsigned int value);
            Event& set(const std::string& key, long value);
            Event& set(const std::string& key, unsigned long value);
            Event& set(const std::string& key, long long value);
            Event& set(const std::string& key, unsigned long long value);
            Event& set(const std::string& key, float value);
            Event& set(const std::string& key, double value);
            Event& set(const std::string& key, const std::string& value);
            Event& set(const std::string& key, const char* value);
            Event& set(const std::string& key, const std::map<std::string, int>& value);

            /** A JSON null, for a field that is genuinely unknown rather than zero. */
            Event& set(const std::string& key, std::nullptr_t value);

            /** The verbatim prose reason, for judgement and never for parsing. */
            Event& detail(const std::string& text);

        private:
            friend class SimEventLog;

            Event(const SimEventLog* log, std::size_t index);

            const SimEventLog* log;
            std::size_t index;
        };

        /** Begins an event at `tick` named `name`. `secs` is derived at flush. */
        Event event(unsigned int tick, const std::string& name) const;

        /** Writes every buffered event to `path`, one JSON object per line. */
        void write(const std::filesystem::path& path) const;

        void clear() const;

        bool empty() const { return events.empty(); }

    private:
        friend class Event;

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

        void addField(std::size_t index, const std::string& key, Value value) const;

        mutable std::vector<Pending> events;
    };
}
