.PHONY: clean build server cmake launcher icon version stop_port

MINGW_CXX := /ucrt64/bin/g++.exe
CMAKE_CONFIG := -DCMAKE_CXX_COMPILER=$(MINGW_CXX)
CMAKE_BUILD := cmake -B build -S . $(CMAKE_CONFIG)
CMAKE_ALL := cmake --build build

STOP_PORT := build/tools/stop-port/ogg.stop-port.exe
VERSIONBUMP := build/tools/versionbump/ogg.versionbump.exe
RUN_SERVER := build\ogg.server.exe

all: build

build: cmake
	$(CMAKE_ALL)

cmake:
	$(CMAKE_BUILD)

icon: cmake
	$(CMAKE_ALL) --target ogg_launcher_icon

stop_port:
	-$(STOP_PORT) 8123 8124 8125

version: stop_port cmake
	$(CMAKE_ALL) --target ogg.versionbump
	$(VERSIONBUMP)
	$(CMAKE_ALL)
	$(RUN_SERVER)

server: stop_port cmake
	$(CMAKE_ALL) --target ogg.server
	$(RUN_SERVER)

launcher: cmake
	$(CMAKE_ALL) --target ogg.launcher ogg.client
	-build\ogg.launcher.exe

reload:
	$(RUN_SERVER) -s reload

clean:
	-cmd //c "if exist build rmdir /s /q build"
	-rm -rf build
