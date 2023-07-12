CXX			= g++
CXXFLAGS 	= -std=c++20 -Wall -Wextra -Wpedantic -g
LDFLAGS		= -lX11

all: clean emu

display.o: display.cpp display.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

emu: main.cpp display.o
	$(CXX) $(CXXFLAGS) main.cpp -o emu display.o $(LDFLAGS)

clean:
	rm -rf *.o emu