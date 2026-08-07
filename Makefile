.PHONY: clean build server

all: build

build: clean cmake

cmake:
	mkdir -p build && cd build && cmake .. && cmake --build .

server: clean
	cmake -B build -S .
	cmake --build build --target ogg.server
	build\ogg.server.exe

reload:
	build\ogg.server.exe -s reload

clean:
	@rm -rf build
