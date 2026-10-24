# Gobe Productive port for Haiku (x86_64). Build on Haiku: make
# The app reads the user's original "Gobe Productive" binary at runtime
# ($GOBE_ORIGINAL, or next to the executable).

NAME := GobeProductive
CXX ?= g++
CXXFLAGS += -std=c++17 -O2 -g -Wall -Wno-multichar -Isrc/core -Isrc/app
LIBS := -lbe -ltracker -ltranslation -ltextencoding -lz

CORE := $(wildcard src/core/*.cpp)
APP := $(wildcard src/app/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(CORE) $(APP))

all: build/$(NAME)

build/$(NAME): $(OBJ) build/$(NAME).rsrc
	$(CXX) -o $@ $(OBJ) $(LIBS)
	xres -o $@ build/$(NAME).rsrc
	mimeset -f $@

build/$(NAME).rsrc: $(NAME).rdef
	@mkdir -p build
	rc -o $@ $<

build/%.o: src/%.cpp $(wildcard src/*/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build

.PHONY: all clean
