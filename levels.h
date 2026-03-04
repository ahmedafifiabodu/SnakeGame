#pragma once

#include <string>
#include <vector>
#include <iostream>

#include "palette.h"

// Hint: what does a level *need* to describe itself?
struct LevelConfig
{
	// name (so you can match "level1" from argv)
	std::string name{};

	// board width & height
	int width{ 0 };
	int height{ 0 };

	// the wall/tile layout (vector of strings works great)
	std::vector<std::string> layout{};

	// snake starting speed (ms per tick?)
	int snakeSpeed{ 200 };

	bool isDynamic{ false };

	int offsetX{ 0 };
	int offsetY{ 0 };

	Palette palette{};
};

LevelConfig loadLevel(std::string levelName);
LevelConfig generateDynamicLevel(std::string levelName);