#pragma once

namespace neoncoil
{
    // Runs a whole multiplayer session -- host, three clients, a full match,
    // a mid-match departure and a host quit -- over real loopback sockets, in
    // one process, with no window. Returns a process exit code.
    //
    // This is the multiplayer equivalent of the level generator self-test: the
    // networking layer is the part of this game that cannot be verified by
    // looking at a screenshot, so it verifies itself instead.
    int runNetworkSelfTest();
}
