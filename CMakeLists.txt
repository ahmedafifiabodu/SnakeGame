cmake_minimum_required(VERSION 3.22)

project(NeonCoil
    VERSION 0.2.0
    DESCRIPTION "NEON COIL - arcade snake"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed to compile src/app.rc, which carries the executable's icon. Enabled
# here rather than in project() so a Linux relay build never looks for a
# resource compiler it has no use for.
if(WIN32)
    enable_language(RC)
endif()

# Single output tree so the game, its shaders and any future content sit
# together and can be zipped straight into a Steam depot.
set(NEONCOIL_OUTPUT "${CMAKE_BINARY_DIR}/bin")
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} "${NEONCOIL_OUTPUT}")
endforeach()

# ---------------------------------------------------------------- dependencies

include(FetchContent)

# Build only the relay, and nothing that needs a screen.
#
# The relay runs on a headless Linux VPS. Without this, configuring the project
# there would build SFML's Graphics, Window and Audio modules too, and demand
# X11, OpenGL, FreeType and ALSA development packages the relay never uses. With
# it, a bare server needs a compiler and CMake and nothing else.
option(NEONCOIL_RELAY_ONLY "Build only the relay server, with no graphics dependencies" OFF)

# Static SFML: Steam builds ship as one executable rather than a pile of DLLs.
set(BUILD_SHARED_LIBS       OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK      ON  CACHE BOOL "" FORCE)   # multiplayer transport
set(SFML_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_DOC          OFF CACHE BOOL "" FORCE)
set(SFML_USE_STATIC_STD_LIBS OFF CACHE BOOL "" FORCE)

# Fetch SFML's own dependencies rather than hunting for system ones.
#
# SFML defaults this OFF on Windows and ON everywhere else, which means the
# Network module calls find_package(MbedTLS REQUIRED) on Linux and fails with
# "Could NOT find MbedTLS" on any box that has not been prepared. Installing the
# distribution package is not a fix either: SFML 3.1 fetches mbedTLS 3.6.5, while
# Debian 12 ships 2.28, and the two are not API compatible -- so the configure
# would succeed and the compile would fail instead.
#
# Forcing it off makes every platform build the same pinned dependencies from
# source, which is the whole reason this project vendors SFML through
# FetchContent in the first place. It costs build time on a fresh machine and
# removes an entire class of "works on mine".
set(SFML_USE_SYSTEM_DEPS    OFF CACHE BOOL "" FORCE)

if(NEONCOIL_RELAY_ONLY)
    set(SFML_BUILD_GRAPHICS OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_WINDOW   OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_AUDIO    OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        3.1.0
    GIT_SHALLOW    ON
    SYSTEM)
FetchContent_MakeAvailable(SFML)

# The client transport resolves and connects on a worker thread so the menu does
# not freeze for the length of a TCP timeout. Both targets need it.
find_package(Threads REQUIRED)

# ------------------------------------------------------------------ the game

if(NOT NEONCOIL_RELAY_ONLY)

# The executable's own icon and version info. Windows only -- and distinct from
# the window icon the game sets at runtime, which does nothing for the file as it
# sits in a folder or on a taskbar before launch.
set(NEONCOIL_RESOURCES "")
if(WIN32)
    set(NEONCOIL_RESOURCES src/app.rc)
endif()

add_executable(NeonCoil
    ${NEONCOIL_RESOURCES}

    src/main.cpp
    src/App.cpp

    src/core/BlockFont.cpp
    src/core/GlyphAtlas.cpp
    src/core/Input.cpp
    src/core/Rng.cpp
    src/core/Settings.cpp
    src/core/Textures.cpp
    src/core/Screen.cpp

    src/game/Ability.cpp
    src/game/Food.cpp
    src/game/Level.cpp
    src/game/LevelGenerator.cpp
    src/game/MatchSimulation.cpp
    src/game/MatchSnapshot.cpp
    src/game/Progression.cpp
    src/game/Snake.cpp
    src/game/SnakeType.cpp

    src/net/ClientSession.cpp
    src/net/HostSession.cpp
    src/net/Identity.cpp
    src/net/Lobby.cpp
    src/net/Matchmaker.cpp
    src/net/NetConfig.cpp
    src/net/Protocol.cpp
    src/net/RelayProtocol.cpp
    src/net/RelayTransport.cpp
    src/net/SocketOptions.cpp
    src/net/TcpTransport.cpp

    src/relay/RelayServer.cpp

    src/states/GameState.cpp
    src/states/LobbyState.cpp
    src/states/MenuState.cpp
    src/states/MultiplayerMenuState.cpp
    src/states/NetPlayState.cpp
    src/states/OptionsState.cpp
    src/states/OverlayStates.cpp
    src/states/PlayState.cpp

    src/tools/Autoplay.cpp
    src/tools/NetDemo.cpp
    src/tools/NetTest.cpp

    src/ui/Art.cpp
    src/ui/Draw.cpp
    src/ui/Effects.cpp
    src/ui/Hit.cpp
    src/ui/Hud.cpp
    src/ui/Palette.cpp
    src/ui/SnakeCard.cpp
)

target_include_directories(NeonCoil PRIVATE src)

target_link_libraries(NeonCoil PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
    SFML::Network)

target_link_libraries(NeonCoil PRIVATE Threads::Threads)

# The game reads netconfig.txt from its working directory, so it has to sit
# beside the executable rather than at the top of the source tree. Copied after
# every build so editing the relay address does not need a reconfigure.
add_custom_command(TARGET NeonCoil POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/netconfig.txt"
        "$<TARGET_FILE_DIR:NeonCoil>/netconfig.txt"
    COMMENT "Placing netconfig.txt beside the executable")

if(MSVC)
    target_compile_options(NeonCoil PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(NeonCoil PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)

    # Release ships without a console window behind the game.
    set_target_properties(NeonCoil PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${NEONCOIL_OUTPUT}")
else()
    target_compile_options(NeonCoil PRIVATE -Wall -Wextra)
endif()

endif()   # NOT NEONCOIL_RELAY_ONLY

# ------------------------------------------------------------------- the relay
#
# Rendezvous and byte-pump for players who cannot accept inbound connections.
# Both the host and its guests dial OUT to this, so nobody forwards a port.
#
# Deliberately links only Network and System: it opens no window, needs no GPU,
# no X11 and no audio, so it builds and runs on a bare Linux box. It also shares
# no game code beyond the relay protocol and the seeded RNG -- it cannot know the
# rules, which is what lets it stay up untouched across game updates.
add_executable(neoncoil-relay
    src/relay/main_relay.cpp
    src/relay/RelayServer.cpp

    src/core/Rng.cpp
    src/net/RelayProtocol.cpp
    src/net/SocketOptions.cpp
)

target_include_directories(neoncoil-relay PRIVATE src)
target_link_libraries(neoncoil-relay PRIVATE SFML::Network SFML::System Threads::Threads)

if(MSVC)
    target_compile_options(neoncoil-relay PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(neoncoil-relay PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(neoncoil-relay PRIVATE -Wall -Wextra)
endif()

