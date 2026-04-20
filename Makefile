# Emergency Dispatch Simulator — build with MSYS2 gcc (or any C11 compiler).
#
# Targets:
#   make            build libdispatch.a + dispatch.exe (TUI)
#   make shared     also build libdispatch.dll for FFI (Python/Web bindings)
#   make tests      build all test_*.exe binaries
#   make run        run the TUI simulator
#   make clean

CC       ?= gcc
CSTD     ?= -std=c11
CWARN    ?= -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
COPT     ?= -O2 -g
CDEFS    ?=
CFLAGS   ?= $(CSTD) $(CWARN) $(COPT) $(CDEFS) -Iinclude
LDFLAGS  ?=
LDLIBS   ?= -lm

BUILD    := build
LIB_A    := $(BUILD)/libdispatch.a
LIB_DLL  := $(BUILD)/libdispatch.dll
BIN      := $(BUILD)/dispatch.exe

DS_SRC   := $(wildcard src/ds/heaps/*.c) \
            $(wildcard src/ds/trees/*.c) \
            $(wildcard src/ds/strings/*.c) \
            $(wildcard src/ds/randomized/*.c) \
            $(wildcard src/ds/spatial/*.c) \
            $(wildcard src/ds/misc/*.c) \
            $(wildcard src/common/*.c)
SIM_SRC  := $(wildcard src/sim/*.c)
TUI_SRC  := $(wildcard src/tui/*.c)

LIB_SRC  := $(DS_SRC) $(SIM_SRC)
LIB_OBJ  := $(LIB_SRC:%.c=$(BUILD)/%.o)
TUI_OBJ  := $(TUI_SRC:%.c=$(BUILD)/%.o)

TEST_SRC := $(wildcard tests/*.c)
TEST_BIN := $(TEST_SRC:tests/%.c=$(BUILD)/test_%.exe)

.PHONY: all shared tests run clean dirs

all: dirs $(LIB_A) $(BIN)

shared: dirs $(LIB_DLL)

tests: dirs $(LIB_A) $(TEST_BIN)

run: all
	$(BIN)

dirs:
	@mkdir -p $(BUILD)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB_A): $(LIB_OBJ)
	ar rcs $@ $^

$(LIB_DLL): $(LIB_SRC)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LDLIBS) -Wl,--out-implib,$(BUILD)/libdispatch.dll.a

$(BIN): $(TUI_OBJ) $(LIB_A)
	$(CC) $(CFLAGS) -o $@ $(TUI_OBJ) $(LIB_A) $(LDFLAGS) $(LDLIBS)

$(BUILD)/test_%.exe: tests/%.c $(LIB_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_A) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILD)
