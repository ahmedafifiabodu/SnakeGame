#include "main.h"

int main(int argc, char* argv[])
{
	// 1. Parse argv → get level name → call loadLevel()
	// if argc < 2 → handle missing argument
	if (argc < 2)
	{
		std::cerr << "Usage: " << argv[0] << " <levelName>" << std::endl;
		return 1;
	}

	std::srand(std::time(nullptr));

	//levelName = argv[1]
	std::string levelName = argv[1];

	// config = loadLevel(levelName)
	LevelConfig config = loadLevel(levelName);

	// 2. setupCustomConsole()
	setupCustomConsole(config.width, config.height);

	clearBuffer();

	// 3. Game loop:
		//    a. Read input
		//    b. Update game state
		//    c. Render

	renderLevel(config);
	renderBuffer();

	Sleep(10000);

	// 4. deleteCustomConsole()
	deleteCustomConsole();
}