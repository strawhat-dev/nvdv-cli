#!/bin/bash

cpp_flags=(
  -O3
  -Wall
  -Wextra
  -Werror
  -Wno-cast-function-type
  -march=native
  -std=c++20
  -lshell32
  -luser32
)

cd "$(dirname "$0")" && \
  rm -rf out && \
  mkdir out && \
  clang++.exe "${cpp_flags[@]}" \
  main.cpp -o out/nvdv.exe
