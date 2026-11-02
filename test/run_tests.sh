#!/usr/bin/env bash
set -e

# Move to the project root directory
CDPATH="" cd -- "$(dirname -- "$0")/.."

echo "Checking dependencies..."

CXXFLAGS=(-std=c++17 -pthread -Icomponents/outequip_ac)
LDFLAGS=(-lgtest -lgmock)

# Check if g++ can locate <gtest/gtest.h> with given flags
check_gtest_include() {
  echo '#include <gtest/gtest.h>' | g++ "${CXXFLAGS[@]}" -E -x c++ - >/dev/null 2>&1
}

# 1. First, check if default include paths resolve <gtest/gtest.h> (standard on Linux / custom toolchains)
if ! check_gtest_include; then
  # 2. If default include paths do not find gtest, check if Homebrew can provide it (macOS or Linux with Homebrew)
  if command -v brew &>/dev/null; then
    BREW_PREFIX=$(brew --prefix)
    CXXFLAGS+=(-I"${BREW_PREFIX}/include")
    LDFLAGS=(-L"${BREW_PREFIX}/lib" "${LDFLAGS[@]}")

    if ! check_gtest_include; then
      if [ "$(uname -s)" = "Darwin" ]; then
        echo "GoogleTest not found in Homebrew paths. Installing via Homebrew..."
        brew install googletest
      fi
    fi
  fi
fi

# 3. Final verification with g++
if ! check_gtest_include; then
  echo "Error: GoogleTest headers (<gtest/gtest.h>) could not be located by g++." >&2
  if [ "$(uname -s)" = "Darwin" ]; then
    echo "Please install GoogleTest (e.g.: brew install googletest)." >&2
  else
    echo "Please install GoogleTest (e.g.: sudo apt-get install libgtest-dev libgmock-dev)." >&2
  fi
  exit 1
fi

# Ensure cleanup of binary on exit
trap 'rm -f test_framer' EXIT

echo "Compiling tests..."
g++ "${CXXFLAGS[@]}" \
  test/test_native/ac_framer_test.cpp \
  components/outequip_ac/ac_framer.cpp \
  "${LDFLAGS[@]}" \
  -o test_framer

echo -e "\nRunning tests..."
./test_framer

echo "Tests completed successfully."
