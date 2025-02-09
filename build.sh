#!/bin/bash

cpp_flags=(
  -O3
  -Wall
  -Wextra
  -Werror
  -luser32
  -lshell32
  -std=c++23
  -march=native
  -Lnvapi/amd64
  -Invapi
)

cd "$(dirname "$0")" && \
  rm -rf out && \
  mkdir out && \
  clang++.exe "${cpp_flags[@]}" \
  src/main.cpp -o out/nvdv.exe
