BENCHMARK_LIBS = -pthread

HEADERS = \
  include/bev/linear_ringbuffer.hpp \
  include/bev/io_buffer.hpp

all: benchmark tests

CXXFLAGS += -Wall -Wextra -pedantic

benchmark: benchmark.cpp $(HEADERS)
	g++-9 $< -O3 -DNDEBUG -std=c++17 -I./include -o $@ $(CFLAGS) $(CXXFLAGS) $(BENCHMARK_LIBS)

tests: tests.cpp $(HEADERS)
	g++ $< -g3 -I./include -o $@ $(CFLAGS) $(CXXFLAGS)


PREFIX ?= /usr
install:
	install -d include/ $(DESTDIR)/$(PREFIX)
