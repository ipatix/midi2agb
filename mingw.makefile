GIT_VERSION := $(shell git describe --abbrev=7 --dirty --always --tags)

CXX = x86_64-w64-mingw32-g++
STRIP = x86_64-w64-mingw32-strip
CXXFLAGS = -Wall -Wextra -Wconversion -std=c++17 -O2 -g -DGIT_VERSION=\"$(GIT_VERSION)\" -flto
BINARY = midi2agb.exe
LIBS = 

SRC_FILES = $(wildcard *.cpp)
OBJ_FILES = $(SRC_FILES:.cpp=.o) cppmidi/cppmidi.o

.PHONY: all clean
all: $(BINARY)

clean:
	rm -f $(OBJ_FILES) $(BINARY)

$(BINARY): $(OBJ_FILES)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS) -static
	$(STRIP) -s $@
