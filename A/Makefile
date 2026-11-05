CXX ?= g++
CXXFLAGS = -std=c++11 -O3 -Wall -Wextra
SMALLFLAGS = $(CXXFLAGS) -pthread -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables
PARFLAGS = $(CXXFLAGS) -pthread
STATIC = -static-libstdc++ -static-libgcc -Wl,--as-needed -no-pie

.PHONY: all clean

all: dtm dtm_par

dtm: dtm.cpp fastio.h
	$(CXX) $(SMALLFLAGS) $(STATIC) -o dtm dtm.cpp 2>/dev/null || \
	$(CXX) $(SMALLFLAGS) -o dtm dtm.cpp

dtm_par: dtm_par.cpp fastio.h
	$(CXX) $(PARFLAGS) $(STATIC) -o dtm_par dtm_par.cpp 2>/dev/null || \
	$(CXX) $(PARFLAGS) -o dtm_par dtm_par.cpp

clean:
	rm -f dtm dtm_par
