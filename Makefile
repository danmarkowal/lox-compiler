# Compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# List all C files and corresponding object files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(SRC_FILES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Executable name
TARGET = $(BIN_DIR)/clox

# Default target (build the program)
all: $(TARGET)

# Rule to link object files into the final executable
$(TARGET): $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJ_FILES) -o $(TARGET)

# Rule to compile each .c file into a .o object file
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Rule to test the compiled program
test: $(TARGET)
	$(TARGET)

# Phony targets
.PHONY: all clean test

