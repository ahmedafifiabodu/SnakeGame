#pragma once

#include <SFML/Network/TcpSocket.hpp>

namespace neoncoil::net
{
    // A TCP socket with Nagle's algorithm turned off.
    //
    // Nagle exists to stop a program that writes one byte at a time from filling
    // the network with tiny packets: it holds a small write back until the
    // previous one has been acknowledged. That is exactly the wrong trade for a
    // game. Every message this project sends is small and every one of them is
    // wanted immediately, so Nagle turns each hop into "wait for an ack first"
    // and stacks a fraction of the round trip onto every input and every
    // snapshot. Over a relay there are four such hops between pressing a key and
    // seeing the result, and the delays add up rather than overlapping.
    //
    // SFML keeps the underlying handle protected, which is why this is a derived
    // class rather than a free function: deriving is the sanctioned way to reach
    // it, and it costs nothing at the use sites -- a GameSocket is an
    // sf::TcpSocket everywhere one is expected.
    class GameSocket : public sf::TcpSocket
    {
    public:
        // Safe to call before a connection exists; it simply does nothing until
        // there is a handle to set the option on.
        void disableNagle();
    };
}
