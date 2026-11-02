#!/usr/bin/env bash
set -e

# Move to the project root directory
CDPATH="" cd -- "$(dirname -- "$0")/.."

echo "Checking dependencies..."

CXXFLAGS="-std=c++17 -pthread -Icomponents/outequip_ac"
LDFLAGS="-lgtest -lgmock"

case "$(uname -s)" in
  Darwin)
    if ! command -v brew &>/dev/null; then
      echo "Error: Homebrew is required on macOS to locate or install GoogleTest." >&2
      exit 1
    fi

    BREW_PREFIX=$(brew --prefix)
    if [ ! -d "${BREW_PREFIX}/opt/googletest" ] && [ ! -d "${BREW_PREFIX}/Cellar/googletest" ]; then
      echo "googletest is not installed. Installing via Homebrew..."
      brew install googletest
    fi

    CXXFLAGS="${CXXFLAGS} -I${BREW_PREFIX}/include"
    LDFLAGS="-L${BREW_PREFIX}/lib ${LDFLAGS}"
    ;;

  Linux)
    if [ ! -f "/usr/include/gtest/gtest.h" ]; then
      echo "Error: GoogleTest headers not found (/usr/include/gtest/gtest.h)." >&2
      echo "Please install libgtest-dev and libgmock-dev (e.g.: sudo apt-get install libgtest-dev libgmock-dev)." >&2
      exit 1
    fi
    ;;

  *)
    echo "Warning: Unrecognized operating system $(uname -s); attempting compilation with default flags..." >&2
    ;;
esac

# Ensure cleanup of binary on exit
trap 'rm -f test_framer' EXIT

echo "Compiling tests..."
# shellcheck disable=SC2086
g++ ${CXXFLAGS} \
  test/test_native/ac_framer_test.cpp \
  components/outequip_ac/ac_framer.cpp \
  ${LDFLAGS} \
  -o test_framer

echo -e "\nRunning tests..."
./test_framer

echo "Tests completed successfully."
