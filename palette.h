#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <iostream>

#include "console.h"

struct Palette
{
	std::string playerName{};

	// Symbols (default choices)
	char wallSymbol{ '#' };
	char snakeSymbol{ '@' };
	char foodSymbol{ 'o' };

	// Colors (console attribute WORDs; use FOREGROUND_/BACKGROUND_ constants)
	WORD snakeColor{ FOREGROUND_GREEN | FOREGROUND_INTENSITY };
	WORD foodColor{ FOREGROUND_RED | FOREGROUND_INTENSITY };
	WORD wallColor{ FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY };
};

Palette selectPalette();