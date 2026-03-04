#pragma once
#include <string>
#include <vector>
#include <iostream>

// Hint: what does a level *need* to describe itself?
struct LevelConfig
{
	// name (so you can match "level1" from argv)
	std::string name;

	// board width & height
	int width;
	int height;

	// the wall/tile layout (vector of strings works great)
	std::vector<std::string> layout;

	// snake starting speed (ms per tick?)
	int snakeSpeed;

	bool isDynamic;

	int offsetX;
	int offsetY;
};

LevelConfig loadLevel(std::string levelName);
LevelConfig generateDynamicLevel(std::string levelName);