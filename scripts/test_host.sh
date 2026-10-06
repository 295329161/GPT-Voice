#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS=halt_on_error=1
cd "$(dirname "$0")/.."
mkdir -p artifacts/tests
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/image_info_test.c src/core/image_info.c -o artifacts/tests/image_info
artifacts/tests/image_info
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/button_test.c src/core/button.c -o artifacts/tests/button
artifacts/tests/button
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/search_intent_test.c src/core/search_intent.c -o artifacts/tests/search_intent
artifacts/tests/search_intent
search_json_dir="${IDF_PATH:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-espidf}/components/json/cJSON"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src -I "$search_json_dir" tests/search_evidence_test.c src/core/search_evidence.c "$search_json_dir/cJSON.c" -lm -o artifacts/tests/search_evidence
artifacts/tests/search_evidence
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/ws_text_test.c src/core/ws_text.c -o artifacts/tests/ws_text
artifacts/tests/ws_text
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/conversation_test.c src/core/conversation.c -o artifacts/tests/conversation
artifacts/tests/conversation
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/resampler_test.c src/core/resampler.c -lm -o artifacts/tests/resampler
artifacts/tests/resampler
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/path_test.c src/core/path.c -o artifacts/tests/path
artifacts/tests/path
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I tests/stubs -I src tests/storage_test.c src/services/storage.c src/core/path.c -Wl,--wrap=opendir,--wrap=closedir,--wrap=readdir,--wrap=stat -o artifacts/tests/storage
artifacts/tests/storage
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/arcade_test.c src/core/arcade.c src/core/tilt.c -lm -o artifacts/tests/arcade
artifacts/tests/arcade
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -I src tests/tilt_test.c src/core/tilt.c -lm -o artifacts/tests/tilt
artifacts/tests/tilt
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I tests/stubs -I components/audio_player tests/mp3_recovery_test.cpp components/audio_player/audio_mp3.cpp -o artifacts/tests/mp3_recovery
artifacts/tests/mp3_recovery
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I tests/stubs -I components/audio_player tests/wav_test.cpp components/audio_player/audio_wav.cpp -o artifacts/tests/wav
artifacts/tests/wav
python3 tests/test_stage.py
