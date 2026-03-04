#include "logic.h"

GameState initGame(const LevelConfig& config)
{
	GameState state;
	state.level = config;
	state.score = 0;
	state.isRunning = true;
	state.isDead = false;
	state.snakeDirection = RIGHT;

	//├── snake starts as 3 segments at center:
	//│   head   = { centerX,     centerY }
	//│   middle = { centerX - 1, centerY }
	//│   tail   = { centerX - 2, centerY }
	//│   → push_back each to state.snake

	int centerX = config.width / 2;
	int centerY = config.height / 2;

	COORD head = { static_cast<SHORT>(centerX), static_cast<SHORT>(centerY) };
	COORD middle = { static_cast<SHORT>(centerX - 1), static_cast<SHORT>(centerY) };
	COORD tail = { static_cast<SHORT>(centerX - 2), static_cast<SHORT>(centerY) };

	state.snake.push_back(head);
	state.snake.push_back(middle);
	state.snake.push_back(tail);

	//  place food at random position that is not on the snake or a wall
	spawnFood(state);

	return state;
}

void updateLogic(GameState& state, const InputState& input)
{
	//├── 1. Handle quit
		//└── if input.quit → state.isRunning = false, return

	if (input.quit) {
		state.isRunning = false;
		return;
	}

	//├── 2. Update direction(NEVER allow reversing)
	//	│   └── if input.direction == UP and current != DOWN → change
	//	│   └── if input.direction == DOWN and current != UP → change
	//	│   └── if input.direction == LEFT and current != RIGHT → change
	//	│   └── if input.direction == RIGHT and current != LEFT → change

	if (input.direction == UP && state.snakeDirection != DOWN)
		state.snakeDirection = UP;
	else if (input.direction == DOWN && state.snakeDirection != UP)
		state.snakeDirection = DOWN;
	else if (input.direction == LEFT && state.snakeDirection != RIGHT)
		state.snakeDirection = LEFT;
	else if (input.direction == RIGHT && state.snakeDirection != LEFT)
		state.snakeDirection = RIGHT;

	//├── 3. Calculate new head position
	//	    └── UP    →{ head.X,     head.Y - 1 }
	//	    └── DOWN  →{ head.X,     head.Y + 1 }
	//	    └── LEFT  →{ head.X - 1, head.Y }
	//	    └── RIGHT →{ head.X + 1, head.Y }

	COORD newHead = state.snake.front();

	switch (state.snakeDirection)
	{
	case UP:
		newHead.Y -= 1;
		break;

	case DOWN:
		newHead.Y += 1;
		break;

	case LEFT:
		newHead.X -= 1;
		break;

	case RIGHT:
		newHead.X += 1;
		break;

	default:
		break;
	}

	//├── 4. Check wall collision
	//   └── if level.layout[newHead.Y][newHead.X] == '#' → isDead = true

	if (state.level.layout[newHead.Y][newHead.X] == '#') {
		state.isDead = true;
		state.isRunning = false;
		return;
	}

	//├── 5. Check self collision
	//   └── loop through snake body — if newHead == any segment → isDead = true

	for (const COORD& segment : state.snake) {
		if (newHead.X == segment.X && newHead.Y == segment.Y) {
			state.isDead = true;
			state.isRunning = false;
			return;
		}
	}

	//6. If isDead → isRunning = false, return

	if (state.isDead) {
		state.isRunning = false;
		return;
	}

	//├── 7. Check food collision
	//   └── if newHead == food → score++, spawnFood(), DON'T pop tail
	//   └── else               → pop_back(remove tail = movement)

	if (newHead.X == state.food.X && newHead.Y == state.food.Y) {
		state.score++; // increment score

		// spawn new food
		spawnFood(state);
	}
	else {
		state.snake.pop_back(); // remove tail
	}

	//├── 8. Add new head to front of snake (movement)
	state.snake.push_front(newHead);
}

void spawnFood(GameState& state)
{
	while (true) {
		int foodX = std::rand() % state.level.width;
		int foodY = std::rand() % state.level.height;

		COORD foodPos = { static_cast<SHORT>(foodX), static_cast<SHORT>(foodY) };
		bool isOnSnake = false;
		for (const COORD& segment : state.snake) {
			if (segment.X == foodPos.X && segment.Y == foodPos.Y) {
				isOnSnake = true;
				break;
			}
		}

		if (!isOnSnake && state.level.layout[foodY][foodX] != '#') {
			state.food = foodPos;
			break;
		}
	}
}