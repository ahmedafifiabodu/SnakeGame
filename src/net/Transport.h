#pragma once

#include <SFML/Network/Packet.hpp>

#include <cstdint>
#include <string>

namespace neoncoil::net
{
    // A transport-level connection. NOT a player slot and NOT an identity: the
    // session layer maps peers onto slots, and slots onto identities, so a
    // different transport (a relay, a Steam P2P channel, a dedicated server
    // socket) can be dropped in without the session layer noticing.
    using PeerId = std::uint32_t;
    inline constexpr PeerId kInvalidPeer = 0;

    enum class TransportEventType
    {
        None,
        Connected,
        Disconnected,
        Message
    };

    struct TransportEvent
    {
        TransportEventType type{ TransportEventType::None };
        PeerId peer{ kInvalidPeer };
        sf::Packet packet;
        std::wstring reason;
    };

    // Both interfaces are poll-driven and non-blocking on purpose: the whole
    // networking layer runs on the game thread inside update(), which removes
    // every locking question and costs at most one frame of latency on a game
    // that steps eight times a second.
    class ServerTransport
    {
    public:
        virtual ~ServerTransport() = default;

        virtual bool start(std::uint16_t port, std::wstring& error) = 0;
        virtual void stop() = 0;

        // Does the actual socket work: accepts, flushes and receives. Called
        // once per frame, before the poll() drain loop, so a flood of inbound
        // traffic can never turn that drain loop into a spin.
        virtual void pump() = 0;
        virtual bool isRunning() const = 0;
        virtual std::uint16_t port() const = 0;

        // Returns false when there is nothing left this frame.
        virtual bool poll(TransportEvent& out) = 0;

        virtual void send(PeerId peer, sf::Packet packet) = 0;
        virtual void broadcast(const sf::Packet& packet) = 0;
        virtual void broadcastExcept(PeerId exclude, const sf::Packet& packet) = 0;
        virtual void disconnect(PeerId peer) = 0;

        virtual int peerCount() const = 0;
    };

    class ClientTransport
    {
    public:
        virtual ~ClientTransport() = default;

        // Non-blocking: returns true once the attempt is under way. Success or
        // failure arrives later as a Connected or Disconnected event.
        virtual bool beginConnect(const std::string& address, std::uint16_t port, std::wstring& error) = 0;
        virtual void disconnect() = 0;
        virtual bool isConnected() const = 0;

        virtual void pump() = 0;
        virtual bool poll(TransportEvent& out) = 0;
        virtual void send(sf::Packet packet) = 0;
    };
}
