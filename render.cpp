#include "render.h"

void renderLevel(const LevelConfig& config)
{
	//  loop through every row y in config.layout:
	//      loop through every character x in that row :
	//          if config.layout[y][x] == '#' :
	//              drawTile(x + state.offsetX, y + state.offsetY, '#', WHITE)

	for (int y = 0; y < config.height; ++y) {
		for (int x = 0; x < config.width; ++x) {
			if (config.layout[y][x] == '#') {
				drawTile(x + config.offsetX, y + config.offsetY, '#', WHITE);
			}
		}
	}
}

void renderSnake(const GameState& state)
{
	// loop through every segment in state.snake:
	//     drawTile(segment.X + state.offsetX, segment.Y + state.offsetY, 'O', GREEN)
	for (const COORD& segment : state.snake)
		drawTile(segment.X + state.level.offsetX, segment.Y + state.level.offsetY, 'O', GREEN);
}

void renderFood(const GameState& state)
{
	//drawTile(state.food.X + state.offsetX, state.food.Y + state.offsetY, 'X', RED)
	drawTile(state.food.X + state.level.offsetX, state.food.Y + state.level.offsetY, 'X', RED);
}

void renderScore(const GameState& state, const LevelConfig& level)
{
	std::string scoreText = "Score: " + std::to_string(state.score);
	int xPosition = level.width - scoreText.length();
	int yPosition = 0;

	for (size_t i = 0; i < scoreText.length(); ++i)
		drawTile(xPosition + i, yPosition, scoreText[i], WHITE);
}

void renderHeader(const std::string& headerText, const LevelConfig& level, const GameState& state)
{
	const int HEADER_BG = BACKGROUND_GREEN | BACKGROUND_INTENSITY;

	// Fill entire row 0 with ' '
	int consoleWidth = level.width + level.offsetX * 2;
	for (int x = 0; x < consoleWidth; ++x)
		drawTile(x, 0, ' ', HEADER_BG);

	// Draw level name on the LEFT
	drawString(1, 0, headerText, HEADER_BG);

	// Draw score on the RIGHT
	std::string scoreText = "Score: " + std::to_string(state.score);
	int scoreXPosition = level.width - scoreText.length() - 1; // -1 for some padding
	drawString(scoreXPosition, 0, scoreText, HEADER_BG);
}

void renderGameOver(const GameState& state, const LevelConfig& level)
{
	// Draw "GAME OVER" centered on the screen
	std::vector<std::string> gameOverArt = {
	"  ######    ###    ##     ## ########         #######  ##     ## ######## ######## ",
	" ##    ##  ## ##   ###   ### ##              ##     ## ##     ## ##       ##     ##",
	" ##       ##   ##  #### #### ##              ##     ## ##     ## ##       ##     ##",
	" ##  #### ##     ## ## ### ## ######         ##     ## ##     ## ######   ######## ",
	" ##    ## ######### ##     ## ##             ##     ##  ##   ##  ##       ##   ##  ",
	" ##    ## ##     ## ##     ## ##             ##     ##   ## ##   ##       ##    ## ",
	"  ######  ##     ## ##     ## ########        #######     ###    ######## ##     ##"
	};

	// Use the full console/buffer width for centering (same formula as renderHeader)
	int consoleWidth = level.width + level.offsetX * 2;

	int startX = (consoleWidth - (int)gameOverArt[0].length()) / 2;
	int startY = level.offsetY + (level.height / 2) - 5; // account for the Y offset

	for (int i = 0; i < (int)gameOverArt.size(); ++i)
		drawString(startX, startY + i, gameOverArt[i], RED);

	// Draw final score below the game over text
	std::string finalScoreText = "Final Score: " + std::to_string(state.score);
	int finalScoreX = (consoleWidth - (int)finalScoreText.length()) / 2;
	int finalScoreY = startY + (int)gameOverArt.size() + 1;

	for (size_t i = 0; i < finalScoreText.length(); ++i)
		drawTile(finalScoreX + i, finalScoreY, finalScoreText[i], YELLOW);
}