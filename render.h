#pragma once

#include "console.h"
#include "levels.h"
#include "logic.h"

void renderLevel(const LevelConfig& config);
void renderSnake(const GameState& state);
void renderFood(const GameState& state);
void renderScore(const GameState& state, const LevelConfig& level);
void renderHeader(const std::string& headerText, const LevelConfig& level, const GameState& state);
void renderGameOver(const GameState& state, const LevelConfig& level);
void renderPauseScreen(const LevelConfig& level);