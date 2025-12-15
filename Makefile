EXE = registry
SRC = program4.cpp
CXX = g++
CFLAGS = -std=c++17 -Wall -g 

all: $(EXE)

$(EXE): $(SRC)
	$(CXX) $(CFLAGS) $(SRC) -o $(EXE)

clean:
	rm -f $(EXE)

.PHONY: all clean

