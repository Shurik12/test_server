# Project name
PROJECT_NAME = server

# Directories
BUILD_DIR = build
SRC_DIR = src

# Compiler and tools
CXX = clang++-18
CC = clang-18
CMAKE = cmake
NINJA = ninja
MKDIR = mkdir -p
RM = rm -rf

# Build type (Debug/Release)
BUILD_TYPE ?= Debug

# Number of parallel jobs for ninja
JOBS ?= 4

# Default target
.DEFAULT_GOAL := help

# Phony targets
.PHONY: all configure build run clean help debug release test install

# Main targets
all: configure build

## Configure the project
configure:
	@echo "Configuring project..."
	@$(MKDIR) $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DBUILD_TESTING=ON

## Build the project
build: configure
	@echo "Building project..."
	@cd $(BUILD_DIR) && $(NINJA) -j$(JOBS)

## Build with debug symbols
debug: BUILD_TYPE = Debug
debug: clean all

## Build for release (optimized)
release: BUILD_TYPE = Release
release: clean all

## Run the service
run: build
	@echo "Starting $(PROJECT_NAME)..."
	@cd $(BUILD_DIR) && ./$(PROJECT_NAME)

## Run in background (for testing)
run-background: build
	@echo "Starting $(PROJECT_NAME) in background..."
	@cd $(BUILD_DIR) && ./$(PROJECT_NAME) &

## Clean build directory
clean:
	@echo "Cleaning build directory..."
	@$(RM) $(BUILD_DIR)

## Run tests (if any)
test: build
	@echo "Running tests..."
	@cd $(BUILD_DIR) && $(NINJA) test

## Install the service
install: build
	@echo "Installing $(PROJECT_NAME)..."
	@cd $(BUILD_DIR) && $(NINJA) install

## Create logs directory
logs:
	@$(MKDIR) logs

## Show this help message
help:
	@echo "Available targets:"
	@echo "  all        - Configure and build the project (default)"
	@echo "  configure  - Configure CMake project in build directory"
	@echo "  build      - Build the project using Ninja"
	@echo "  run        - Build and run the service"
	@echo "  run-background - Build and run in background"
	@echo "  debug      - Clean and build with debug symbols"
	@echo "  release    - Clean and build with optimizations"
	@echo "  clean      - Remove build directory"
	@echo "  test       - Run tests (if configured)"
	@echo "  install    - Install the service"
	@echo "  logs       - Create logs directory"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  BUILD_TYPE - Build type: Debug or Release (default: Debug)"
	@echo "  JOBS       - Number of parallel jobs for ninja (default: 4)"
	@echo ""
	@echo "Examples:"
	@echo "  make                         # Configure and build"
	@echo "  make debug                   # Build with debug symbols"
	@echo "  make run                     # Build and run"
	@echo "  make BUILD_TYPE=Debug run    # Build debug and run"
	@echo "  make JOBS=8 build            # Build with 8 parallel jobs"

## Development targets
dev: debug run

## Quick build (assumes already configured)
quick-build:
	@cd $(BUILD_DIR) && $(NINJA) -j$(JOBS)

## Reconfigure (clean and configure)
reconfigure: clean configure

## Build and show size
size: build
	@echo "Binary size:"
	@size $(BUILD_DIR)/$(PROJECT_NAME) 2>/dev/null || echo "Size command not available"

## Generate compile_commands.json for IDE support
compile-commands: configure
	@cd $(BUILD_DIR) && $(NINJA) -j$(JOBS)
	@echo "compile_commands.json generated in $(BUILD_DIR)"

## Format code (if clang-format is available)
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting source code..."; \
		find $(SRC_DIR) -name "*.h" -o -name "*.cpp" | xargs clang-format -i; \
		echo "Formatting complete."; \
	else \
		echo "clang-format not found. Install it to format code."; \
	fi

## Static analysis (if clang-tidy is available)
analyze: compile-commands
	@if command -v clang-tidy >/dev/null 2>&1; then \
		echo "Running static analysis..."; \
		find $(SRC_DIR) -name "*.cpp" | xargs -I {} clang-tidy {} -p $(BUILD_DIR); \
	else \
		echo "clang-tidy not found. Install it for static analysis."; \
	fi

## Create distribution tarball
dist: clean
	@echo "Creating distribution tarball..."
	@tar -czf $(PROJECT_NAME)-$(shell date +%Y%m%d).tar.gz --exclude=$(BUILD_DIR) --exclude=".*" .

## Show build information
info:
	@echo "Project: $(PROJECT_NAME)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Build type: $(BUILD_TYPE)"
	@echo "Source directory: $(SRC_DIR)"
	@echo "Compiler: $(CXX)"
	@echo "CMake generator: Ninja"
	@echo "Parallel jobs: $(JOBS)"