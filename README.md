# Snake Game

A simple console-based Snake game written in C++ for Windows. This repository contains the game code, level definitions, and a small custom console renderer that sizes the console to the active level.

## Features
- Terminal rendering using the Windows Console API (`WriteConsoleOutput`).
- Multiple levels defined in `levels.cpp` (`level1`, `level2`, `level3`).
- Keyboard input with buffered direction to make controls responsive.
- Custom console setup to size the buffer and hide the cursor.

## Requirements
- Windows 10 or later
- Visual Studio (MSVC) with C++ support or any toolchain that can build a Win32 console application

## Build
1. Open the `Assessment 2.sln` (or `Assessment 2.vcxproj`) in Visual Studio and build the solution.
2. Or build from the Developer Command Prompt:

```
cl /EHsc /W4 /Fe:SnakeGame.exe *.cpp
```

Make sure you build as a Win32 console application and link against the default Win32 libraries.

## Usage
Run the executable with a level name as the only argument. Example:

```
SnakeGame.exe level3
```

If no level name is supplied the program will print a usage message and exit.

## Controls
- Arrow keys or `W`/`A`/`S`/`D` to change the snake direction.
- `Esc` to quit.

## Levels
Levels are defined in `levels.cpp`. Each level contains a width, height and a static ASCII layout. The available level names in this repository are:
- `level1`
- `level2`
- `level3`

You can add more levels by editing `levels.cpp` and returning a new `LevelConfig` in `loadLevel`.

## Notes
- The console is sized by the program to match the level dimensions plus padding. If the console window cannot fit the requested size at the current font, the program attempts to set a smaller font so the entire game board is visible without a scroll bar.
- The main game loop is in `main.cpp`. Rendering helpers are in `console.cpp` and `render.cpp`.

## License
This project is provided as-is for assessment purposes. Feel free to reuse or modify the code for learning.

## Credits
Author: Ahmed Afifi (repository origin). Project adapted for assessment and demonstration of Windows console rendering.

