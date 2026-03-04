#include "palette.h"

Palette selectPalette()
{
	Palette palette; // Create a Palette object

	// Step 1: Ask for player name
	std::cout << "Enter your name: ";
	std::cin >> palette.playerName;
	system("cls");

	// Step 2: Ask for color (validate input and re-prompt on error)
	int colorChoice = 0;
	while (true)
	{
		system("cls");

		std::cout << "Select a color:\n";
		std::cout << "1. Red\n2. Green\n3. Blue\n4. Yellow\n";

		if (!(std::cin >> colorChoice))
		{
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

			std::cout << "\nInvalid input! Press Enter to try again...";
			std::cin.get();
			std::cin.get();
			continue;
		}

		if (colorChoice < 1 || colorChoice > 4)
		{
			std::cout << "\nChoice out of range! Press Enter to try again...";
			std::cin.get();
			std::cin.get();
			continue;
		}

		break;
	}

	// Map the validated choice to a color (WORD console attributes)
	switch (colorChoice) {
	case 1:
		palette.snakeColor = FOREGROUND_RED | FOREGROUND_INTENSITY; // Red
		break;
	case 2:
		palette.snakeColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Green
		break;
	case 3:
		palette.snakeColor = FOREGROUND_BLUE | FOREGROUND_INTENSITY; // Blue
		break;
	case 4:
		palette.snakeColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; // Yellow
		break;
	default:
		palette.snakeColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // should not happen
		break;
	}
	system("cls");

	// Step 3: Ask for symbol (validate input and re-prompt on error)
	int symbolChoice = 0;
	while (true)
	{
		system("cls");

		std::cout << "Select a symbol for the snake:\n";
		std::cout << "1. @\n2. &\n3. %\n4. +\n";

		if (!(std::cin >> symbolChoice))
		{
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

			std::cout << "\nInvalid input! Press Enter to try again...";
			std::cin.get();
			continue;
		}

		if (symbolChoice < 1 || symbolChoice > 4)
		{
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

			std::cout << "\nChoice out of range! Press Enter to try again...";
			std::cin.get();
			continue;
		}

		std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		break;
	}

	// Map the validated choice to a symbol
	switch (symbolChoice) {
	case 1:
		palette.snakeSymbol = '@';
		break;
	case 2:
		palette.snakeSymbol = '&';
		break;
	case 3:
		palette.snakeSymbol = '%';
		break;
	case 4:
		palette.snakeSymbol = '+';
		break;
	default:
		palette.snakeSymbol = '@'; // default fallback
		break;
	}

	system("cls");
	return palette;
}