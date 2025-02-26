#!/bin/bash

CPPFLAGS=(
  -O3
  -Wall
  -Wextra
  -Werror
  -luser32
  -lshell32
  -std=c++23
)

cd "$(dirname "$0")" && \
  { [[ -d nvapi/amd64 ]] || git submodule update --init --remote; } && \
  rm -f nvdv.exe && clang++.exe "${CPPFLAGS[@]}" nvdv.cpp -o nvdv.exe
