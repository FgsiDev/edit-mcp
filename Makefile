CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
PREFIX   ?= /usr/local
BIN      := edit-mcp
HEADERS  := $(wildcard src/*.hpp)

all: $(BIN)

$(BIN): src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ src/main.cpp

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

skill: $(BIN)
	mkdir -p skills/edit-mcp
	./$(BIN) skill > skills/edit-mcp/SKILL.md

clean:
	rm -f $(BIN)

.PHONY: all install skill clean
