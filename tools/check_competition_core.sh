#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .pio/competition-host
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
  tests/competition_core.cpp -o .pio/competition-host/core_test
.pio/competition-host/core_test
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
  tests/control_timing.cpp -o .pio/competition-host/control_timing_test
.pio/competition-host/control_timing_test
