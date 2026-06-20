CXX = c++
CXXFLAGS = -std=gnu++17 -Wall
INCLUDES = -I ./include
LDFLAGS = -L ./lib
LIBS = -ltvision -lncurses
TARGET = visic4

all: $(TARGET)

$(TARGET): visic4.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f $(TARGET)

rebuild: clean all

.PHONY: all clean rebuild
