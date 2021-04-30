#!/bin/bash
set -xeuo pipefail
clang-format -i **/*.cc *.cpp *.h
for F in `ls **/*.php`
do
  hackfmt -i $F
done
