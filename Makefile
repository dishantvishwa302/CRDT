CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
LIBS     := -lpthread -lrt

SRCS := main.cpp display.cpp registry.cpp ipc.cpp crdt.cpp monitor.cpp

.PHONY: all clean

all: editor

editor: $(SRCS) types.h display.h registry.h ipc.h crdt.h monitor.h
	$(CXX) $(CXXFLAGS) -o editor $(SRCS) $(LIBS)

clean:
	rm -f editor *_doc.txt
