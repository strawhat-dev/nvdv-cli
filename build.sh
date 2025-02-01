#!/bin/bash

cpp_flags=(
  -O3
  -Wall
  -Wextra
  -Werror
  -luser32
  -lshell32
  -Lnvapi/lib
  -Invapi/include
  -march=native
  -std=c++20
)

cd "$(dirname "$0")" && \
  rm -rf out && \
  mkdir out && \
  clang++.exe "${cpp_flags[@]}" \
  main.cpp -o out/nvdv.exe
