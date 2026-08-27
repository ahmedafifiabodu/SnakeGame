#pragma once

#include <memory>
#include <string>

namespace neoncoil::net
{
    class NetGame;
    class HostSession;
}

namespace neoncoil
{
    namespace tools
    {
        // A real four-player session, running inside one process over loopback,
        // for the capture tooling to photograph.
        //
        // The alternative was a mock lobby with invented names, and that would
        // have been a lie in the one place this project has been careful not to
        // tell one: every screenshot in the README is the build. This is a real
        // host, real clients, the real handshake and the real simulation -- the
        // only thing staged is that all four players happen to be on the same
        // machine and steered by code instead of hands.
        class NetDemo
        {
        public:
            NetDemo();
            ~NetDemo();

            NetDemo(const NetDemo&) = delete;
            NetDemo& operator=(const NetDemo&) = delete;

            // Opens a host and connects `guests` clients to it, then blocks
            // briefly until they have all taken a seat. Returns false, with a
            // reason, if the session could not be opened at all.
            bool start(int guests, bool readyUp, std::wstring& error);

            // Pumps the guests and steers them. Call once per frame.
            //
            // It deliberately does NOT pump the host: once a screen owns the
            // host session, that screen pumps it, and doing it here as well
            // would advance the simulation twice per frame and run the match at
            // double speed.
            void tick(float deltaSeconds);

            void startMatch();
            void stop();

            // The host session, for a screen to render. Null before start().
            net::NetGame* host() const;

            // Hands ownership of the host to a screen (LobbyState takes a
            // unique_ptr). This keeps steering the guests afterwards; it just
            // stops being responsible for the host's lifetime.
            std::unique_ptr<net::HostSession> takeHost();

        private:
            struct Impl;
            std::unique_ptr<Impl> m_impl;
        };
    }
}
