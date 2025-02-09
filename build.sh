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
  -std=c++23
)

cd "$(dirname "$0")/src" && \
  rm -rfv ../out && \
  mkdir -v ../out && \
  clang++.exe "${cpp_flags[@]}" \
  main.cpp -o ../out/nvdv.exe
