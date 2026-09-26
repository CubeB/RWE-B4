#include "ControlChannel.h"

#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <rwe/util/SimpleLogger.h>
#include <thread>

namespace rwe
{
    namespace
    {
        std::mutex& stateMutex()
        {
            static std::mutex m;
            return m;
        }

        std::vector<ControlRequest>& pending()
        {
            static std::vector<ControlRequest> requests;
            return requests;
        }

        void writeLine(const nlohmann::json& j)
        {
            static std::mutex writeMutex;
            std::scoped_lock<std::mutex> lock(writeMutex);

            // Flushed a line at a time, because the reader on the other end is
            // waiting for this one and the game will not write another for
            // minutes. std::endl says both halves of that.
            std::cout << j.dump() << std::endl;
        }

        void readLoop()
        {
            std::string line;
            while (std::getline(std::cin, line))
            {
                if (line.empty())
                {
                    continue;
                }

                ControlRequest request;
                try
                {
                    auto j = nlohmann::json::parse(line);
                    request.command = j.value("command", std::string());
                    request.player = j.value("player", 0u);
                }
                catch (const std::exception& e)
                {
                    // Said and skipped. A launcher that sends nonsense should
                    // not be able to stop the game it is watching.
                    LOG_ERROR << "Bridge: could not read a command: " << e.what();
                    continue;
                }

                if (request.command.empty())
                {
                    LOG_ERROR << "Bridge: a command with no command field: " << line;
                    continue;
                }

                std::scoped_lock<std::mutex> lock(stateMutex());
                pending().push_back(request);
            }

            LOG_INFO << "Bridge: standard input closed; no more commands will be read";
        }
    }

    void ControlChannel::start()
    {
        if (on)
        {
            return;
        }

        on = true;
        std::thread(readLoop).detach();
        LOG_INFO << "Bridge: listening for commands on standard input";
    }

    std::vector<ControlRequest> ControlChannel::take()
    {
        if (!on)
        {
            return {};
        }

        std::scoped_lock<std::mutex> lock(stateMutex());
        auto out = std::move(pending());
        pending().clear();
        return out;
    }

    void ControlChannel::sendPlayerDropped(unsigned int player, unsigned int fromTick)
    {
        if (!on)
        {
            return;
        }

        writeLine(nlohmann::json{
            {"event", "player-dropped"},
            {"player", player},
            {"tick", fromTick},
        });
    }

    void ControlChannel::sendRejoinBundle(unsigned int player, unsigned int atTick, const std::string& file)
    {
        if (!on)
        {
            return;
        }

        writeLine(nlohmann::json{
            {"event", "rejoin-bundle"},
            {"player", player},
            {"tick", atTick},
            {"file", file},
        });
    }

    void ControlChannel::sendRejoinRefused(unsigned int player, const std::string& reason)
    {
        if (!on)
        {
            return;
        }

        writeLine(nlohmann::json{
            {"event", "rejoin-refused"},
            {"player", player},
            {"reason", reason},
        });
    }

    void ControlChannel::sendGameEnded(const nlohmann::json& report)
    {
        if (!on)
        {
            return;
        }

        writeLine(report);
    }

    ControlChannel& getControlChannel()
    {
        static ControlChannel* instance = new ControlChannel();
        return *instance;
    }
}
