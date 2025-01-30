#!/bin/bash

cpp_flags=(
  -O3
  -Wall
  -luser32
  -lshell32
  -std=c++20
  -march=native
)

cd "$(dirname "$0")" || exit 1

rm -rf out && mkdir out && clang.exe "${cpp_flags[@]}" main.cpp -o out/nvdv.exe
