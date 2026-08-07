all: build

build: clean cmake

cmake:
	mkdir -p build && cd build && cmake .. && cmake --build .

clean:
	@rm -rf build
