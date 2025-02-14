#!/bin/bash

CPPFLAGS=(
  -O3
  -Wall
  -Wextra
  -Werror
  -Invapi
  -luser32
  -lshell32
  -std=c++23
  -Lnvapi/amd64
)

cd "$(dirname "$0")" && rm -rf out && mkdir out && \
  { [[ -d nvapi/amd64 ]] || git submodule update --init --remote; } && \
  clang++.exe "${CPPFLAGS[@]}" main.cpp -o out/nvdv.exe
