#include "input.h"

InputState readInput()
{
	InputState inputState = { NONE, false };

	if (getIfUpKeyIsCurrentlyDown())
		inputState.direction = UP;
	else if (getIfDownKeyIsCurrentlyDown())
		inputState.direction = DOWN;
	else if (getIfLeftKeyIsCurrentlyDown())
		inputState.direction = LEFT;
	else if (getIfRightKeyIsCurrentlyDown())
		inputState.direction = RIGHT;
	else if (getIfEscKeyIsCurrentlyDown())
		inputState.quit = true;
	else
		inputState.direction = NONE;

	if (getIfPauseKeyIsCurrentlyDown())
		inputState.pause = true;

	return inputState;
}

int getIfBasicKeyIsCurrentlyDown(char key)
{
	return (GetAsyncKeyState(key) & 0x8000) != 0;
}

int getIfUpKeyIsCurrentlyDown()
{
	return (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
}

int getIfDownKeyIsCurrentlyDown()
{
	return (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
}

int getIfLeftKeyIsCurrentlyDown()
{
	return (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
}

int getIfRightKeyIsCurrentlyDown()
{
	return (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
}

int getIfEscKeyIsCurrentlyDown()
{
	return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

int getIfPauseKeyIsCurrentlyDown()
{
	return (GetAsyncKeyState('P') & 0x8000) != 0;
}