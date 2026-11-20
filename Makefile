# Makefile for Windows development via MSYS2 GCC (PC target)
# Usage:
#   make            # build debug executable
#   make run        # build and run
#   make clean      # remove build artifacts

CXX      ?= g++
CC       ?= gcc

TARGET   ?= hpak.exe
BUILD_DIR ?= build/pc

# Collect sources
SRCS_CPP := $(shell find src -type f -name '*.cpp')
SRCS_C   := lib/miniz/miniz.c

OBJS_CPP := $(SRCS_CPP:%=$(BUILD_DIR)/%.o)
OBJS_C   := $(SRCS_C:%=$(BUILD_DIR)/%.o)
OBJS     := $(OBJS_CPP) $(OBJS_C)
DEPS     := $(OBJS:.o=.d)

CPPFLAGS ?=
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -g -O0
CFLAGS   ?= -std=c11 -Wall -Wextra -g -O0
LDFLAGS  ?=
LDLIBS   ?=

INCLUDES := -Isrc -Ilib -Ilib/json -Ilib/miniz

# Silence noisy warnings from vendored miniz
$(BUILD_DIR)/lib/miniz/miniz.c.o: CFLAGS += -Wno-unused-function

.PHONY: all run clean

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

run: $(BUILD_DIR)/$(TARGET)
	$(BUILD_DIR)/$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
