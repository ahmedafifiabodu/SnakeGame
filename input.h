#pragma once
#include <windows.h>

enum Direction { UP, DOWN, LEFT, RIGHT, NONE };

struct InputState
{
	Direction direction;
	bool quit;
	bool pause;
};

InputState readInput();

int getIfBasicKeyIsCurrentlyDown(char key);
int getIfUpKeyIsCurrentlyDown();
int getIfDownKeyIsCurrentlyDown();
int getIfLeftKeyIsCurrentlyDown();
int getIfRightKeyIsCurrentlyDown();
int getIfEscKeyIsCurrentlyDown();
int getIfPauseKeyIsCurrentlyDown();