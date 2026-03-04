#include "render.h"

void renderLevel(const LevelConfig& config)
{
	//  loop through every row y in config.layout:
	//      loop through every character x in that row :
	//          if config.layout[y][x] == '#' :
	//              drawTile(x, y, '#', someWallColor)

	for (int y = 0; y < config.height; ++y) {
		for (int x = 0; x < config.width; ++x) {
			if (config.layout[y][x] == '#') {
				drawTile(x, y, '#', WHITE);
			}
		}
	}
}