# FullCopy - Windows build with MinGW-w64.
#
#   make docker   Builds the Docker image and compiles inside it (recommended).
#   make          Compiles directly (needs x86_64-w64-mingw32-g++ installed).
#   make clean    Removes the build files.
#   make manual-tree         Builds the tool that creates the manual test data.
#   make docker-manual-tree  Builds that tool inside Docker.
#
# The executable name includes the version: bin/fullcopy-$(VERSION).exe

VERSION   := 1.0.2

CXX       := x86_64-w64-mingw32-g++
WINDRES   := x86_64-w64-mingw32-windres

SRC_DIR   := src
INC_DIR   := include
RES_DIR   := resources
BUILD_DIR := build
BIN_DIR   := bin
TARGET    := $(BIN_DIR)/fullcopy-$(VERSION).exe

SOURCES   := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS   := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
RESOURCE  := $(BUILD_DIR)/fullcopy.res.o
DEPENDS   := $(OBJECTS:.o=.d)

CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -I$(INC_DIR) \
             -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -MMD -MP
LDFLAGS   := -municode -mwindows -static -static-libgcc -static-libstdc++
LDLIBS    := -lcomctl32 -lshell32 -lole32 -luuid

# Tool that creates the source tree for the manual checks (docs/testing.md).
MANUAL_TREE := $(BIN_DIR)/generate_manual_tree.exe
TOOLFLAGS   := -std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601

IMAGE     := fullcopy-builder
CONTAINER ?= docker

.PHONY: all clean docker docker-image debug docker-debug manual-tree docker-manual-tree

all: $(TARGET)

debug:
	$(MAKE) all BUILD_DIR=build/debug BIN_DIR=bin/debug \
		CXXFLAGS="$(filter-out -O2,$(CXXFLAGS)) -Og -g3 \
		-D_GLIBCXX_ASSERTIONS -fstack-protector-strong \
		-Wformat=2 -Wshadow -Wconversion"

docker-debug: docker-image
	$(CONTAINER) run --rm -u $$(id -u):$$(id -g) \
		-v "$(CURDIR)":/project:Z $(IMAGE) make debug

$(TARGET): $(OBJECTS) $(RESOURCE) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(RESOURCE): $(RES_DIR)/fullcopy.rc $(RES_DIR)/fullcopy.manifest | $(BUILD_DIR)
	$(WINDRES) -I$(RES_DIR) $< -O coff -o $@

$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

manual-tree: $(MANUAL_TREE)

$(MANUAL_TREE): tests/generate_manual_tree.cpp | $(BIN_DIR)
	$(CXX) $(TOOLFLAGS) -o $@ $< -municode -static

docker-manual-tree: docker-image
	$(CONTAINER) run --rm -u $$(id -u):$$(id -g) \
		-v "$(CURDIR)":/project:Z $(IMAGE) make manual-tree

docker-image:
	$(CONTAINER) build -t $(IMAGE) .

docker: docker-image
	$(CONTAINER) run --rm -u $$(id -u):$$(id -g) -v "$(CURDIR)":/project:Z $(IMAGE) make

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(DEPENDS)
