#!/usr/bin/env bash
# Plays the bot through the official MIT Pokerbots 2024 engine (engine.py from
# github.com/gtowizard-ai/mitpoker-2024, which ships the competition engine).
# Usage: scripts/run_mit_engine.sh [opponent: self|caller]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OPP=${1:-self}
WORK="$ROOT/build/mit"
mkdir -p "$WORK/bot" "$WORK/caller"
[ -f "$WORK/engine.py" ] || curl -sL https://raw.githubusercontent.com/gtowizard-ai/mitpoker-2024/main/engine.py -o "$WORK/engine.py"
printf '{"build": [], "run": ["%s/build/pokerbot"]}\n' "$ROOT" > "$WORK/bot/commands.json"
printf '{"build": [], "run": ["%s/.venv/bin/python", "%s/scripts/caller_bot.py"]}\n' "$ROOT" "$ROOT" > "$WORK/caller/commands.json"
P2="./bot"; [ "$OPP" = caller ] && P2="./caller"
cat > "$WORK/config.py" <<CFG
PLAYER_1_NAME = 'MainBot'
PLAYER_1_PATH = './bot'
PLAYER_2_NAME = 'Opponent'
PLAYER_2_PATH = '$P2'
GAME_LOG_FILENAME = 'gamelog'
PLAYER_LOG_SIZE_LIMIT = 524288
ENFORCE_GAME_CLOCK = True
STARTING_GAME_CLOCK = 30.
BUILD_TIMEOUT = 10.
CONNECT_TIMEOUT = 10.
NUM_ROUNDS = 1000
STARTING_STACK = 400
BIG_BLIND = 2
SMALL_BLIND = 1
CFG
cd "$WORK" && "$ROOT/.venv/bin/python" engine.py
tail -n 2 gamelog.txt
grep -h "pokerbot:" MainBot.txt Opponent.txt 2>/dev/null || true
