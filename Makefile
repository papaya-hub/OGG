.PHONY: clean build server cmake

MINGW_CXX := /ucrt64/bin/g++.exe
CMAKE_CONFIG := -DCMAKE_CXX_COMPILER=$(MINGW_CXX)

all: build

build: cmake

cmake:
	cmake -B build -S . $(CMAKE_CONFIG)
	cmake --build build

server: cmake
	cmake --build build --target ogg.server
	build\ogg.server.exe

reload:
	build\ogg.server.exe -s reload

clean:
	-cmd //c "if exist build rmdir /s /q build"
	-rm -rf build
