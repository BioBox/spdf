SOURCES = main.cc
OBJECTS = $(SOURCES:.cc=.o)

CXXFLAGS ?= -Wall -O0 -g

lpdf: $(OBJECTS)
	$(CXX) -lpoppler $(OBJECTS) -o $@

%.o: %.cc
	$(CXX) $(CXXFLAGS) -I/usr/include/poppler -c $< -o $@

clean:
	rm -f lpdf $(OBJECTS)
