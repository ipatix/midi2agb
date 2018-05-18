CXX = clang++
STRIP = strip
CXXFLAGS = -Wall -Wextra -Wconversion -std=c++14 -Og -g
BINARY = midi2agb
LIBS = 

SRC_FILES = $(wildcard *.cpp)
OBJ_FILES = $(SRC_FILES:.cpp=.o) cppmidi/cppmidi.o

all: $(BINARY)
	

.PHONY: clean
clean:
	rm -f $(OBJ_FILES)

$(BINARY): $(OBJ_FILES)
	$(CXX) -o $@ $^ $(LIBS)
	#$(STRIP) -s $@

%.o: %.c
	$(CXX) -c -o $@ $< $(CFLAGS) -fsanitize=memory
