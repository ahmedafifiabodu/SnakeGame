#include "App.h"

#include "tools/NetTest.h"

#include <iostream>

int main(int argc, char* argv[])
{
    const neoncoil::LaunchOptions options = neoncoil::parseArguments(argc, argv);

    if (options.invalid)
    {
        std::wcerr << L"Error: " << options.error << L"\n\n";
        neoncoil::printHelp();
        return 2;
    }

    if (options.showHelp)
    {
        neoncoil::printHelp();
        return 0;
    }

    // Before anything dispatches: every mode below can touch the network layer,
    // including the offscreen ones that render the multiplayer screens.
    neoncoil::applyNetworkOptions(options);

    // The self-test runs entirely on the console's normal stdout: it must not
    // touch the game screen, so it returns before App is ever constructed.
    if (options.selfTestLevels > 0)
        return neoncoil::runGeneratorSelfTest(options.selfTestLevels, options.seed);

    // Same contract as the generator self-test: console only, no App, no window.
    if (options.netSelfTest)
        return neoncoil::runNetworkSelfTest();

    if (options.dumpLevel > 0)
        return neoncoil::dumpLevelToConsole(options.dumpLevel, options.seed);

    if (!options.uiDump.empty())
        return neoncoil::dumpUserInterface(options.uiDump, options);

    if (!options.screenshot.empty())
        return neoncoil::captureScreenshot(options.screenshot, options.screenshotPath, options);

    if (!options.capture.empty())
        return neoncoil::captureSequence(options.capture, options.captureDir, options);

    neoncoil::App app(options);
    return app.run();
}
