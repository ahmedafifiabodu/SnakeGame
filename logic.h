#pragma once

#include <windows.h>
#include <deque>

#include "input.h"
#include "levels.h"

struct GameState
{
	std::deque<COORD> snake;   // head at front, tail at back
	COORD food{ 0, 0 };
	Direction snakeDirection{ RIGHT };
	int score{ 0 };
	bool isRunning{ false };
	bool isDead{ false };
	LevelConfig level;         // current level data
	bool isPaused{ false };
};

GameState initGame(const LevelConfig& config);
void updateLogic(GameState& state, const InputState& input);

void spawnFood(GameState& state);