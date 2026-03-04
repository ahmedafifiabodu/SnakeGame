#include "main.h"

#include <chrono>
#include <algorithm>

int main(int argc, char* argv[])
{
	// 1. Parse argv → get level name → call loadLevel()
	// if argc < 2 → handle missing argument
	if (argc < 2)
	{
		std::cerr << "Usage: " << argv[0] << " <levelName>" << std::endl;
		return 1;
	}

	std::srand(static_cast<unsigned int>(std::time(nullptr)));

	//levelName = argv[1]
	std::string levelName = argv[1];

	// config = loadLevel(levelName)
	LevelConfig config = loadLevel(levelName);

	// 2. setupCustomConsole(config.width + PADDING * 2, config.height + PADDING * 2 + 1);
	const int PADDING = 2;
	const int HEADER = 1;
	const int GAME_OVER_ART_WIDTH = 116;

	int consoleWidth = std::max(config.width + PADDING * 2, GAME_OVER_ART_WIDTH + PADDING * 2);
	int consoleHeight = config.height + PADDING * 2 + HEADER;

	config.offsetX = (consoleWidth - config.width) / 2; // center the game board horizontally
	config.offsetY = PADDING + HEADER;

	setupCustomConsole(consoleWidth, consoleHeight);

	config.palette = selectPalette();

	// 3. Game loop:
	//    a. Read input
	//    b. Update game state
	//    c. Render

	GameState gameState = initGame(config);

	auto lastTick = std::chrono::steady_clock::now();
	Direction bufferedDirection = RIGHT;

	while (gameState.isRunning)
	{
		// Always read input
		InputState inputState = readInput();

		if (inputState.quit)
		{
			gameState.isRunning = false;
			continue;
		}

		if (inputState.direction != NONE)
			bufferedDirection = inputState.direction;

		// Only tick the game when enough time has passed
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count();

		if (elapsed >= config.snakeSpeed)
		{
			inputState.direction = bufferedDirection;
			updateLogic(gameState, inputState);

			clearBuffer();

			renderLevel(config);
			renderSnake(gameState);
			renderFood(gameState);
			renderHeader("Level: " + levelName, config, gameState);
			renderBuffer();

			lastTick = now;
		}

		Sleep(10);
	}

	if (gameState.isDead)
	{
		clearBuffer();
		renderGameOver(gameState, config);
		renderBuffer();

		// Wait for user to press ESC before exiting
		while (true)
		{
			if (getIfEscKeyIsCurrentlyDown())
				break;

			Sleep(10);
		}
	}

	// 4. deleteCustomConsole()
	deleteCustomConsole();
}