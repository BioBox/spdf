SOURCES = main.cc rectangle.cc coordconv.cc
OBJECTS = $(SOURCES:.cc=.o)

CXXFLAGS ?= -Wall -O0 -g

lpdf: $(OBJECTS)
	$(CXX) -lpoppler -lX11 $(OBJECTS) -o $@

%.o: %.cc config.h
	$(CXX) -std=c++17 $(CXXFLAGS) -I/usr/include/poppler -c $< -o $@

config.h:
	cp config.def.h config.h

clean:
	rm -f lpdf $(OBJECTS)
