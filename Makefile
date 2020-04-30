GIT_VERSION := $(shell git describe --abbrev=7 --dirty --always --tags)

CXX = g++
STRIP = strip
CXXFLAGS = -Wall -Wextra -Wconversion -std=c++14 -O2 -g -DGIT_VERSION=\"$(GIT_VERSION)\"
BINARY = midi2agb
LIBS = 

SRC_FILES = $(wildcard *.cpp)
OBJ_FILES = $(SRC_FILES:.cpp=.o) cppmidi/cppmidi.o

.PHONY: all clean
all: $(BINARY)

clean:
	rm -f $(OBJ_FILES) $(BINARY)

$(BINARY): $(OBJ_FILES)
	$(CXX) -o $@ $^ $(LIBS)
	#$(STRIP) -s $@
