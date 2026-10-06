#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts/tests
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/ws_text_test.c src/core/ws_text.c -o artifacts/tests/ws_text
artifacts/tests/ws_text
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/conversation_test.c src/core/conversation.c -o artifacts/tests/conversation
artifacts/tests/conversation
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/resampler_test.c src/core/resampler.c -lm -o artifacts/tests/resampler
artifacts/tests/resampler
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/path_test.c src/core/path.c -o artifacts/tests/path
artifacts/tests/path
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/arcade_test.c src/core/arcade.c -lm -o artifacts/tests/arcade
artifacts/tests/arcade
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I tests/stubs -I components/audio_player tests/mp3_recovery_test.cpp components/audio_player/audio_mp3.cpp -o artifacts/tests/mp3_recovery
artifacts/tests/mp3_recovery
python3 tests/test_stage.py
