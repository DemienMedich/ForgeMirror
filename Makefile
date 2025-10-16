# Thin Makefile wrapper around CMake

BUILD_DIR ?= build
CONFIG ?= Release

.PHONY: all release debug configure build run clean

all: release

configure:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CONFIG)

build: configure
	@cmake --build $(BUILD_DIR) --config $(CONFIG)

release:
	@$(MAKE) CONFIG=Release build

debug:
	@$(MAKE) CONFIG=Debug build

run: build
	@echo Running JobSkill...
	@if [ -x "$(BUILD_DIR)/JobSkill" ]; then \
		"$(BUILD_DIR)/JobSkill"; \
	elif [ -x "$(BUILD_DIR)/$(CONFIG)/JobSkill" ]; then \
		"$(BUILD_DIR)/$(CONFIG)/JobSkill"; \
	elif [ -x "$(BUILD_DIR)/$(CONFIG)/JobSkill.exe" ]; then \
		"$(BUILD_DIR)/$(CONFIG)/JobSkill.exe"; \
	else \
		echo "Executable not found. Try 'make build' first."; \
		exit 1; \
	fi

clean:
	@echo Cleaning $(BUILD_DIR)...
	@cmake -E rm -rf $(BUILD_DIR)

