#include "SocketOptions.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace neoncoil::net
{
    void GameSocket::disableNagle()
    {
        const sf::SocketHandle handle = getNativeHandle();

        // SFML does not expose its "no socket yet" sentinel, so the platform's
        // own is compared against here. Calling this before a connection exists
        // is allowed, and has to do nothing rather than poke at a stale handle.
#ifdef _WIN32
        if (handle == INVALID_SOCKET)
            return;

        // Failure is deliberately ignored throughout. Turning Nagle off is a
        // latency improvement, not a requirement: a platform that refuses still
        // plays, just with the delay back in the path.
        const BOOL on = TRUE;
        (void)::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
            reinterpret_cast<const char*>(&on), sizeof(on));
#else
        if (handle == -1)
            return;

        const int on = 1;
        (void)::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
#endif
    }
}
