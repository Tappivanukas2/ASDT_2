# Makefile for RLE-based maze program
# Compilation flags: g++ with pthread and optimization

CXX = g++
CXXFLAGS = -pthread -O2 -w
LDFLAGS = -pthread
TARGET = maze_runner
SOURCES = source.cpp
OBJECTS = $(SOURCES:.cpp=.o)
RLE_FILE = maze.rle

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^
	@echo "Build complete: $(TARGET)"
	@echo "RLE file ready: $(RLE_FILE)"

# Compile source files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Run the program with RLE maze
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Clean all including RLE file
clean-all: clean
	rm -f $(RLE_FILE)

# Help target
help:
	@echo "Available targets:"
	@echo "  all       - Build the executable (default)"
	@echo "  run       - Build and run the maze program"
	@echo "  clean     - Remove build artifacts"
	@echo "  clean-all - Remove all generated files including RLE"
	@echo "  help      - Show this help message"

.PHONY: all run clean clean-all help
