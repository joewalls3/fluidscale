# Compiler settings
CXX = g++
CXXFLAGS = -Wall -std=c++17 -O2

# Library dependencies
LIBS = -lpigpio -lmicrohttpd -pthread

# Directories
SRC_DIR = src
BUILD_DIR = build

# Source files
SRCS = $(SRC_DIR)/fluid_measurement_server.cpp $(SRC_DIR)/wifi_setup.cpp

# Object files
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Target executable
TARGET = fluid_measurement_server

# Default target
all: $(BUILD_DIR)/$(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files to object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Link object files
$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

# Clean build files
clean:
	rm -rf $(BUILD_DIR)

# Install the application
install: $(BUILD_DIR)/$(TARGET)
	sudo mkdir -p /opt/fluid_measurement
	sudo mkdir -p /opt/fluid_measurement/web
	sudo cp $(BUILD_DIR)/$(TARGET) /opt/fluid_measurement/
	sudo cp -r web/* /opt/fluid_measurement/web/
	sudo cp fluid-measurement.service /etc/systemd/system/
	sudo systemctl daemon-reload
	sudo systemctl enable fluid-measurement.service
	sudo systemctl start fluid-measurement.service

.PHONY: all clean install
