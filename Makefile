CXXFLAGS ?= -Wall -O0 -g
include ::= $(shell pkg-config --cflags poppler-cpp)
LDLIBS ::= -lX11 $(shell pkg-config --libs poppler-cpp)

spdf: main.o coordconv.o
	$(CXX) $(LDLIBS) $^ -o $@

main.o: main.cpp config.hpp
	$(CXX) -std=c++20 $(CXXFLAGS) $(include) -c $< -o $@

coordconv.o: coordconv.cpp
	$(CXX) -std=c++20 $(CXXFLAGS) $(include) -c $< -o $@

config.hpp:
	cp config.def.hpp config.hpp

clean:
	rm -f spdf *.o
