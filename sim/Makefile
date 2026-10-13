# Simulator für den Mac: make -C sim && sim/dksim
CXX ?= clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -I../core -I../tiles $(shell sdl2-config --cflags) -DDK_HORSES=\"$(abspath ../tools/horses/out)\"
LDLIBS = $(shell sdl2-config --libs)
SRC = main.cpp $(wildcard ../core/*.cpp) $(wildcard ../tiles/*.cpp)

dksim: $(SRC) $(wildcard ../core/*.h) $(wildcard ../tiles/*.h)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDLIBS)

clean:
	rm -f dksim
