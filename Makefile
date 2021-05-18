SOURCES = main.cc
OBJECTS = $(SOURCES:.cc=.o)

CXXFLAGS ?= -Wall -O0 -g

lpdf: $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f lpdf $(OBJECTS)
