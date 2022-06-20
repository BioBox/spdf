SOURCES = main.cpp rectangle.cpp coordconv.cpp
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS ?= -Wall -O0 -g

lpdf: $(OBJECTS)
	$(CXX) -lpoppler -lX11 $(OBJECTS) -o $@

%.o: %.cpp config.hpp
	$(CXX) -std=c++17 $(CXXFLAGS) -I/usr/include/poppler -c $< -o $@

config.h:
	cp config.def.hpp config.hpp

clean:
	rm -f lpdf $(OBJECTS)
