all: build

build:

cmake:
	mkdir -p build && cd build && cmake .. && cmake --build .
