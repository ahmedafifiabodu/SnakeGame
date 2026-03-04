#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>

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
void drawTile(int x, int y, char c, WORD colors);
void drawTile(int x, int y, char c);
void drawString(int x, int y, std::string s, WORD colors);
void drawString(int x, int y, std::string s);
