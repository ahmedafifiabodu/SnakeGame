#pragma once

#include <windows.h>
#include <deque>

#include "input.h"
#include "levels.h"

struct GameState
{
	std::deque<COORD> snake;   // head at front, tail at back
	COORD food;
	Direction snakeDirection;
	int score;
	bool isRunning;
	bool isDead;
	LevelConfig level;         // current level data
};

GameState initGame(const LevelConfig& config);
void updateLogic(GameState& state, const InputState& input);

void spawnFood(GameState& state);