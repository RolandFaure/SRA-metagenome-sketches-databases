# DNA to Vector Conversion - Makefile
# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -fPIC
CPPFLAGS = -I./include
LDFLAGS =
QUERY_CPPFLAGS = $(shell pkg-config --cflags eigen3 2>/dev/null)
QUERY_CXXFLAGS = -fopenmp
QUERY_LDFLAGS = -fopenmp

# GCC 8 and older require an explicit filesystem library at link time.
GCC_MAJOR := $(shell $(CXX) -dumpversion | cut -d. -f1)
ifeq ($(shell [ $(GCC_MAJOR) -lt 9 ] && echo yes),yes)
LDFLAGS += -lstdc++fs
endif

# Directories
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
OBJ_DIR = obj

# Files
DNA_SOURCES = $(SRC_DIR)/main.cpp $(SRC_DIR)/random_projection.cpp
QUERY_SOURCES = $(SRC_DIR)/query.cpp

DNA_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(DNA_SOURCES))
QUERY_OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(QUERY_SOURCES))

DNA_EXECUTABLE = $(BIN_DIR)/dna_to_vector
QUERY_EXECUTABLE = $(BIN_DIR)/query

# Targets
.PHONY: all clean help

all: $(DNA_EXECUTABLE) $(QUERY_EXECUTABLE)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(INC_DIR)/random_projection.h | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/query.o: $(SRC_DIR)/query.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(QUERY_CXXFLAGS) $(CPPFLAGS) $(QUERY_CPPFLAGS) -c $< -o $@

$(DNA_EXECUTABLE): $(DNA_OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(DNA_OBJECTS) $(LDFLAGS) -o $@
	@echo "✓ Build successful: $(DNA_EXECUTABLE)"

$(QUERY_EXECUTABLE): $(QUERY_OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(QUERY_CXXFLAGS) $(QUERY_OBJECTS) $(LDFLAGS) $(QUERY_LDFLAGS) -o $@
	@echo "✓ Build successful: $(QUERY_EXECUTABLE)"

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

help:
	@echo "Available targets:"
	@echo "  make          - Build all executables"
	@echo "  make $(DNA_EXECUTABLE) - Build DNA to vector converter"
	@echo "  make $(QUERY_EXECUTABLE) - Build query tool"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this message"

.PRECIOUS: $(OBJ_DIR)/%.o
