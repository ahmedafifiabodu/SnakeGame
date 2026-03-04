#include "console.h"
#include <algorithm>

static HANDLE outputHandle = nullptr;
static HANDLE inputHandle = nullptr;

static std::vector<CHAR_INFO> buffer;
static int bufferWidth = 0;
static int bufferHeight = 0;

static DWORD originalConsoleMode = 0;
static CONSOLE_CURSOR_INFO originalCursorInfo = {};

void setupCustomConsole(int width, int height)
{
	bufferWidth = width;
	bufferHeight = height;
	// The static_cast is a visual studio recommendation
	buffer.resize(static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(bufferWidth) * bufferHeight); // Resize the buffer to fit the specified width and height

	//Get the console handles
	outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	inputHandle = GetStdHandle(STD_INPUT_HANDLE);

	// Set the console window size to 1x1 to avoid issues when resizing the buffer
	SMALL_RECT minWindow = { 0, 0, 1, 1 };
	SetConsoleWindowInfo(outputHandle, TRUE, &minWindow);

	// Set the console buffer size to match the desired width and height
	COORD bufferSize = { (SHORT)width, (SHORT)height };
	SetConsoleScreenBufferSize(outputHandle, bufferSize);

	// Set the console window size to match the buffer size
	SMALL_RECT fullWindow = { 0, 0, (SHORT)(width - 1), (SHORT)(height - 1) };
	SetConsoleWindowInfo(outputHandle, TRUE, &fullWindow);

	// Hide the cursor
	GetConsoleCursorInfo(outputHandle, &originalCursorInfo);
	CONSOLE_CURSOR_INFO cursorInfo = originalCursorInfo;
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(outputHandle, &cursorInfo);

	//Disable Quick Edit Mode
	GetConsoleMode(inputHandle, &originalConsoleMode);
	DWORD consoleMode = originalConsoleMode;
	consoleMode &= ~ENABLE_QUICK_EDIT_MODE;
	SetConsoleMode(inputHandle, consoleMode);
}

void deleteCustomConsole()
{
	//1. Restore cursor visibility
	SetConsoleCursorInfo(outputHandle, &originalCursorInfo);

	//2. Restore original console mode
	SetConsoleMode(inputHandle, originalConsoleMode);
}

void clearBuffer()
{
	//	for every y in HEIGHT:
	//		for every x in WIDTH :
	//			buffer[y][x].Char.AsciiChar = ' '
	//			buffer[y][x].Attributes = default color
	// look up for: memset() to clear the buffer faster than nested loops

	for (int y = 0; y < bufferHeight; ++y) {
		for (int x = 0; x < bufferWidth; ++x) {
			buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Char.AsciiChar = ' ';
			buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // default color (white)
		}
	}
}

void fillBuffer(char c)
{
	for (int y = 0; y < bufferHeight; ++y) {
		for (int x = 0; x < bufferWidth; ++x) {
			buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Char.AsciiChar = c;
			buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // default color (white)
		}
	}
}

void renderBuffer()
{
	SMALL_RECT region = { 0, 0, (SHORT)(bufferWidth - 1), (SHORT)(bufferHeight - 1) }; // Define the region to write to (top-left corner to bottom-right corner)
	WriteConsoleOutput(outputHandle, (CHAR_INFO*)buffer.data(), { (SHORT)bufferWidth, (SHORT)bufferHeight }, { 0, 0 }, &region); // Write the buffer to the console
}

void drawTile(int x, int y, char c, WORD colors)
{
	if (x < 0 || x >= bufferWidth) return;
	if (y < 0 || y >= bufferHeight) return;

	buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Char.AsciiChar = c;
	buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Attributes = colors;
}

void drawTile(int x, int y, char c)
{
	if (x < 0 || x >= bufferWidth) return;
	if (y < 0 || y >= bufferHeight) return;

	buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Char.AsciiChar = c;
	buffer[static_cast<std::vector<CHAR_INFO, std::allocator<CHAR_INFO>>::size_type>(y) * bufferWidth + x].Attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // default color (white)
}

void drawString(int x, int y, std::string s, WORD colors)
{
	for (size_t i = 0; i < s.length(); i++)
		drawTile(x + (int)i, y, s[i], colors);
}

void drawString(int x, int y, std::string s)
{
	for (size_t i = 0; i < s.length(); i++)
		drawTile(x + (int)i, y, s[i]);
}