#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Hint: color can be a simple typedef or enum wrapping Windows WORD attributes
// FOREGROUND_RED, FOREGROUND_GREEN, FOREGROUND_BLUE are Windows constants
// e.g. FOREGROUND_RED | FOREGROUND_GREEN = yellow

enum color
{
	BLACK = 0,
	BLUE = 1,
	GREEN = 2,
	CYAN = 3,
	RED = 4,
	MAGENTA = 5,
	YELLOW = 6,
	WHITE = 7
};

void setupCustomConsole(int width, int height);
void deleteCustomConsole();

void clearBuffer();
void fillBuffer(char c);
void renderBuffer();
void drawTile(int x, int y, char c, color colors);
void drawTile(int x, int y, char c);
void drawString(int x, int y, std::string s, color colors);
void drawString(int x, int y, std::string s);
