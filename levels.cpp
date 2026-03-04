#include "levels.h"

LevelConfig loadLevel(std::string levelName)
{
	//  loadLevel("level1") → return a config with layout A, speed X
	//	loadLevel("level2") → return a config with layout B, speed Y
	//	loadLevel("level3") → return a config with layout C, speed Z
	//	loadLevel("dynamic") → call a separate function to generate one

	if (levelName == "level1")
	{
		return {
			"level1",
			50, 30,
			{
				"##################################################",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                                #",
				"#                                    #########   #",
				"#   #########                                    #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                                #",
				"#                                    #########   #",
				"#   #########                                    #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#                                                #",
				"##################################################",
			},
			200,
			false
		};
	}
	else if (levelName == "level2")
	{
		return {
			"level2",
			50, 30,
			{
				"##################################################",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                                #",
				"#                                    #########   #",
				"#   #########                                    #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                                #",
				"#                                    #########   #",
				"#   #########                                    #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#                                                #",
				"##################################################",
			},
			150,
			false
		};
	}
	else if (levelName == "level3")
	{
		return {
			"level3",
			50, 30,
			{
				"##################################################",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                                #",
				"#                                    #########   #",
				"#   #########                                    #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                                #",
				"#                                    #########   #",
				"#   #########                                    #",
				"#                                                #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#   #########                                    #",
				"#                                    #########   #",
				"#                                                #",
				"#                                                #",
				"##################################################",
			},
			100,
			false
		};
	}
	else if (levelName == "dynamic")
	{
		return generateDynamicLevel(levelName);
	}
}

LevelConfig generateDynamicLevel(std::string levelName)
{
	// start with an empty grid filled with ' '
	LevelConfig config;
	config.name = levelName;
	config.width = std::rand() % 50 + 30; // width (30-80)
	config.height = std::rand() % 30 + 20; //  height (20-50)
	config.snakeSpeed = 200; // default speed
	config.layout = std::vector<std::string>(config.height, std::string(config.width, ' '));

	// draw a border of '#' around the edges
	for (int y = 0; y < config.height; ++y) {
		for (int x = 0; x < config.width; ++x) {
			if (y == 0 || y == config.height - 1 || x == 0 || x == config.width - 1) {
				config.layout[y][x] = '#';
			}
		}
	}

	//  scatter some random '#' wall tiles inside
		//  use std::rand() or <random> for positions
	for (int i = 0; i < 10; ++i) {
		int x = std::rand() % (config.width - 2) + 1;
		int y = std::rand() % (config.height - 2) + 1;
		config.layout[y][x] = '#';
	}

	//  make sure the snake's start position stays clear!
	int startX = config.width / 2;
	int startY = config.height / 2;
	config.layout[startY][startX] = ' '; // ensure center is clear

	return config;
}