#include "RelayServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{
    std::atomic<bool> g_running{ true };

    void onSignal(int)
    {
        g_running.store(false);
    }

    void printHelp()
    {
        std::cout <<
            "NEON COIL relay\n"
            "\n"
            "Rendezvous and byte-pump for players who cannot accept inbound\n"
            "connections. Hosts and guests both dial OUT to this process, so\n"
            "nobody has to forward a port.\n"
            "\n"
            "It never parses a game message and holds no game state, so it does\n"
            "not need redeploying when the game changes.\n"
            "\n"
            "Usage: neoncoil-relay [options]\n"
            "\n"
            "  --port <n>      Port to listen on (default 45700).\n"
            "  --idle <secs>   Drop a session with no traffic for this long\n"
            "                  (default 300).\n"
            "  --quiet         Do not log sessions opening and closing.\n"
            "  --stats <secs>  Print a one-line summary this often (default 60,\n"
            "                  0 to disable).\n"
            "  --help          Show this message.\n";
    }
}

int main(int argc, char* argv[])
{
    std::uint16_t port = 45700;
    float idleTimeout = 300.0f;
    float statsInterval = 60.0f;
    bool verbose = true;

    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        const bool hasValue = (i + 1) < argc;

        if (argument == "--help" || argument == "-h")
        {
            printHelp();
            return 0;
        }
        if (argument == "--port" && hasValue)
        {
            port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        }
        else if (argument == "--idle" && hasValue)
        {
            idleTimeout = static_cast<float>(std::atof(argv[++i]));
        }
        else if (argument == "--stats" && hasValue)
        {
            statsInterval = static_cast<float>(std::atof(argv[++i]));
        }
        else if (argument == "--quiet")
        {
            verbose = false;
        }
        else
        {
            std::cerr << "unrecognised argument: " << argument << "\n\n";
            printHelp();
            return 2;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    neoncoil::relay::RelayServer relay;
    relay.setVerbose(verbose);
    relay.setIdleTimeoutSeconds(idleTimeout);

    std::wstring error;
    if (!relay.start(port, error))
    {
        std::wcerr << L"Error: " << error << L"\n";
        return 1;
    }

    std::cout << "NEON COIL relay listening on port " << relay.port() << "\n";

    // A 4ms tick is well inside the game's twenty-hertz snapshot rate and keeps
    // the process at a fraction of a core when nothing is connected.
    constexpr float kStep = 0.004f;
    const auto stepDuration = std::chrono::milliseconds(4);
    float sinceStats = 0.0f;

    while (g_running.load())
    {
        relay.pump();
        relay.tickTimers(kStep);

        if (statsInterval > 0.0f)
        {
            sinceStats += kStep;
            if (sinceStats >= statsInterval)
            {
                sinceStats = 0.0f;
                const neoncoil::relay::RelayStats& stats = relay.stats();
                std::cout << "[relay] sessions " << stats.liveSessions
                          << " live / " << stats.sessionsOpened << " total"
                          << ", connections " << stats.liveConnections
                          << ", forwarded " << (stats.bytesForwarded / 1024) << " KiB\n";
            }
        }

        std::this_thread::sleep_for(stepDuration);
    }

    std::cout << "\nshutting down\n";
    relay.stop();
    return 0;
}
