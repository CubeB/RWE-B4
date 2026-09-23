#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

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
     *
     * **The buffer is held behind a pointer, and that is not an ornament.**
     * A GameSimulation owns one of these by value, and GameSimulation.h is
     * included by most of the engine, so anything this header instantiates is
     * instantiated hundreds of times over. Declaring the value variant here
     * cost 580 COFF sections in GameSimulation.cpp alone -- against a ceiling
     * that is a CI check -- because a std::variant of eleven alternatives
     * brings its whole visit, copy and move apparatus with it. Behind Impl it
     * is instantiated once, in the .cpp. See CLAUDE.md, "A translation unit
     * can outgrow what a COFF object can describe".
     */
    class SimEventLog
    {
    public:
        SimEventLog();
        ~SimEventLog();

        // Move-only, the buffer being its own. GameSimulation, which holds one
        // by value, is move-only already and for the same kind of reason.
        SimEventLog(SimEventLog&&) noexcept;
        SimEventLog& operator=(SimEventLog&&) noexcept;
        SimEventLog(const SimEventLog&) = delete;
        SimEventLog& operator=(const SimEventLog&) = delete;

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

        /**
         * Starts recording. Nothing is kept until this is called.
         *
         * Only the arena ever writes this log, and only a run that asked for
         * the arena ever reads it, so an ordinary game that recorded anyway
         * would be filling memory for the length of the match with something
         * nobody will look at -- a long game against the computer produces
         * thousands of events an hour. The events are free to write and are
         * never free to keep.
         */
        void setRecording(bool value) const { recording = value; }

        bool isRecording() const { return recording; }

        /**
         * Begins an event at `tick` named `name`. `secs` is derived at flush.
         *
         * While not recording this returns an Event that discards everything
         * set on it, so a call site never has to ask first.
         */
        Event event(unsigned int tick, const std::string& name) const;

        /** Writes every buffered event to `path`, one JSON object per line. */
        void write(const std::filesystem::path& path) const;

        void clear() const;

        bool empty() const;

    private:
        friend class Event;

        /** The index an Event carries when there is nothing to add it to. */
        static constexpr std::size_t NotRecording = static_cast<std::size_t>(-1);

        /** The buffer, and the value type it holds, both defined in the .cpp. */
        struct Impl;

        mutable bool recording{false};
        std::unique_ptr<Impl> impl;
    };
}
