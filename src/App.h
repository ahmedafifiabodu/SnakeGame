#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace neoncoil
{
    // Parsed command line. Everything here has a sensible default, so the game
    // runs correctly with no arguments at all -- unlike the original, which
    // exited with a usage message.
    struct LaunchOptions
    {
        std::uint64_t seed{ 0 };
        bool seedPinned{ false };
        std::wstring playerName;
        int snakeTypeIndex{ -1 };
        int selfTestLevels{ 0 };  // > 0 runs the generator self-test and exits
        bool netSelfTest{ false };// runs the networking self-test and exits
        int dumpLevel{ 0 };       // > 0 prints that level as ASCII and exits
        int dumpFrames{ 200 };    // frames to simulate before an offscreen dump
        std::string uiDump;       // non-empty renders a screen offscreen and exits
        std::string screenshot;   // screen name for --screenshot
        std::string screenshotPath;
        std::string capture;      // screen name for --capture (PNG sequence)
        std::string captureDir;
        int captureEvery{ 2 };    // save one frame in n while capturing
        int captureSkip{ 0 };     // frames to simulate before the first one is saved
        bool demo{ false };       // autopilot drives the play screen while capturing

        // Networking. All optional: with none of them set, the game reads
        // netconfig.txt if it is there and otherwise uses built-in defaults, so
        // multiplayer needs no configuration to work on a local network.
        std::string netConfigPath{ "netconfig.txt" };
        int hostPort{ 0 };        // > 0 overrides the configured listen port
        int discoveryPort{ 0 };   // > 0 overrides the configured discovery port

        bool showHelp{ false };
        bool invalid{ false };
        std::wstring error;
    };

    LaunchOptions parseArguments(int argc, char* argv[]);
    void printHelp();

    // Resolves networking configuration and installs the identity provider.
    //
    // Called once from main(), before anything dispatches, because every entry
    // point needs it -- not only the one that opens a window. Having this live
    // inside App::run() meant --screenshot and --uidump rendered the multiplayer
    // menu with no relay configured and reported "No relay in this build".
    void applyNetworkOptions(const LaunchOptions& options);

    // Generates levels across a wide range of indices and asserts every
    // playability invariant. Returns a process exit code.
    int runGeneratorSelfTest(int levelsToTest, std::uint64_t seed);

    // Prints one generated level as ASCII, for eyeballing layouts without
    // having to play up to them.
    int dumpLevelToConsole(int levelIndex, std::uint64_t seed);

    // Renders one screen ("menu", "play", "pause", "clear", "over") into an
    // offscreen buffer and prints it as ASCII. A build-time check that panels,
    // captions and meters actually land where the layout says they should.
    int dumpUserInterface(const std::string& which, const LaunchOptions& options);

    // Renders one screen offscreen and writes it to a PNG -- the only way to
    // actually look at the game without launching it.
    int captureScreenshot(const std::string& which, const std::string& path, const LaunchOptions& options);

    // Writes a numbered PNG sequence into `directory`, one frame in
    // `options.captureEvery`, for assembling into an animation. Paired with
    // --demo this is how the README's GIFs are produced.
    int captureSequence(const std::string& which, const std::string& directory, const LaunchOptions& options);

    // Owns the console, the input layer and the state stack, and runs the loop.
    class App
    {
    public:
        explicit App(const LaunchOptions& options);
        int run();

    private:
        LaunchOptions m_options;
    };
}
