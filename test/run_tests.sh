#!/usr/bin/env bash
set -e

# Move to the project root directory
CDPATH="" cd -- "$(dirname -- "$0")/.."

echo "Checking dependencies..."
if ! command -v brew &>/dev/null; then
  echo "Homebrew is required to install googletest."
  exit 1
fi

if [ ! -d "/opt/homebrew/Cellar/googletest" ] && [ ! -d "/usr/local/Cellar/googletest" ]; then
  echo "googletest is not installed. Installing via Homebrew..."
  brew install googletest
fi

# Define paths for Apple Silicon and Intel Macs
BREW_PREFIX=$(brew --prefix)

echo "Compiling tests..."
g++ -std=c++17 \
  -Icomponents/outequip_ac \
  -I"${BREW_PREFIX}/include" \
  -L"${BREW_PREFIX}/lib" \
  test/test_native/ac_framer_test.cpp \
  components/outequip_ac/ac_framer.cpp \
  -lgtest -lgtest_main -lgmock \
  -o test_framer

echo -e "\nRunning tests..."
./test_framer

# Clean up binary
rm test_framer
echo "Tests completed and cleaned up."
