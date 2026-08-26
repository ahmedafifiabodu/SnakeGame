cmake_minimum_required(VERSION 3.22)

project(NeonCoil
    VERSION 0.1.0
    DESCRIPTION "NEON COIL - arcade snake"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single output tree so the game, its shaders and any future content sit
# together and can be zipped straight into a Steam depot.
set(NEONCOIL_OUTPUT "${CMAKE_BINARY_DIR}/bin")
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} "${NEONCOIL_OUTPUT}")
endforeach()

# ---------------------------------------------------------------- dependencies

include(FetchContent)

# Static SFML: Steam builds ship as one executable rather than a pile of DLLs.
set(BUILD_SHARED_LIBS       OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK      OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_DOC          OFF CACHE BOOL "" FORCE)
set(SFML_USE_STATIC_STD_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG        3.1.0
    GIT_SHALLOW    ON
    SYSTEM)
FetchContent_MakeAvailable(SFML)

# ------------------------------------------------------------------ the game

add_executable(NeonCoil
    src/main.cpp
    src/App.cpp

    src/core/BlockFont.cpp
    src/core/GlyphAtlas.cpp
    src/core/Input.cpp
    src/core/Rng.cpp
    src/core/Textures.cpp
    src/core/Screen.cpp

    src/game/Ability.cpp
    src/game/Food.cpp
    src/game/Level.cpp
    src/game/LevelGenerator.cpp
    src/game/Progression.cpp
    src/game/Snake.cpp
    src/game/SnakeType.cpp

    src/states/GameState.cpp
    src/states/MenuState.cpp
    src/states/OverlayStates.cpp
    src/states/PlayState.cpp

    src/tools/Autoplay.cpp

    src/ui/Art.cpp
    src/ui/Draw.cpp
    src/ui/Effects.cpp
    src/ui/Hud.cpp
)

target_include_directories(NeonCoil PRIVATE src)

target_link_libraries(NeonCoil PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio)

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
