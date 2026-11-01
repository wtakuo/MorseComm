# Host-side unit tests and loss simulation (run: make test)
# Recipes use the "target: prerequisites ; command" form, so this file has no
# tab characters.
CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -g
SRC = test_main.cpp
HDRS = $(wildcard ../../*.h)

bin/test_main: $(SRC) $(HDRS) ; @mkdir -p bin && $(CXX) $(CXXFLAGS) -o $@ $(SRC)

test: bin/test_main ; ./bin/test_main

clean: ; rm -rf bin

.PHONY: test clean
