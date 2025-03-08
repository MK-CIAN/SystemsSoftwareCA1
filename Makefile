# Makefile for Report Management Daemon

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -O2 -std=c99 -D_DEFAULT_SOURCE
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
OBJ_DIR = obj
SCRIPT_DIR = scripts

# Source files and objects
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# Executable name
EXECUTABLE = $(BIN_DIR)/report_daemon

# Installation paths
INSTALL_PATH = /usr/sbin
INIT_SCRIPT_PATH = /etc/init.d
CONFIG_PATH = /etc

# Target name for all
.PHONY: all
all: prepare $(EXECUTABLE)

# Prepare target to create necessary directories
.PHONY: prepare
prepare:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

# Link the executable
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# Clean target
.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*
	@echo "Cleaned build files"

# Install target
.PHONY: install
install: all
	@echo "Installing report_daemon..."
	install -m 755 $(EXECUTABLE) $(INSTALL_PATH)
	install -m 755 $(SCRIPT_DIR)/init.sh $(INIT_SCRIPT_PATH)/report_daemon
	@echo "Creating necessary directories..."
	mkdir -p /var/reports/upload
	mkdir -p /var/reports/dashboard
	mkdir -p /var/backups/reports
	mkdir -p /var/log/report_daemon
	@echo "Setting permissions..."
	chmod 775 /var/reports/upload
	chmod 664 /var/reports/dashboard
	chmod 755 /var/backups/reports
	chmod 755 /var/log/report_daemon
	@echo "Installation complete"

# Uninstall target
.PHONY: uninstall
uninstall:
	@echo "Uninstalling report_daemon..."
	rm -f $(INSTALL_PATH)/report_daemon
	rm -f $(INIT_SCRIPT_PATH)/report_daemon
	@echo "Uninstallation complete"

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all       - build the daemon"
	@echo "  clean     - remove build files"
	@echo "  install   - install the daemon"
	@echo "  uninstall - uninstall the daemon"
	@echo "  help      - display this help message"