CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall

all: minigit

minigit: main.cpp ds.h algo.h
	$(CXX) $(CXXFLAGS) -o minigit main.cpp

clean:
	rm -f minigit minigit.exe
