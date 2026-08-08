.PHONY: clean build server cmake launcher icon version stop_port tools client release

ifeq ($(OS),Windows_NT)
MINGW_CXX := /ucrt64/bin/g++.exe
CMAKE_CONFIG := -DCMAKE_CXX_COMPILER=$(MINGW_CXX)
else
CMAKE_CONFIG :=
endif
CMAKE_BUILD := cmake -B build -S . $(CMAKE_CONFIG)
CMAKE_ALL := cmake --build build

STOP_PORT := build/tools/stop-port/ogg.stop-port.exe
VERSIONBUMP := build/tools/versionbump/ogg.versionbump.exe
RUN_SERVER := build\ogg.server.exe
RUN_LAUNCHER := build\ogg.launcher.exe
VERSION_FILE := src/version.txt

all: build

build: cmake
	$(CMAKE_ALL)

cmake:
	$(CMAKE_BUILD)

# Build versionbump + stop-port before stop_port or version (fresh build dir has no tools yet).
tools: cmake
	$(CMAKE_ALL) --target ogg.versionbump ogg.stop-port

stop_port: tools
	-$(STOP_PORT) 8123 8124 8125

icon: cmake
	$(CMAKE_ALL) --target ogg_launcher_icon

# Bump version.txt, re-configure (regenerate version.hpp), then full rebuild.
version: tools
	-$(STOP_PORT) 8123 8124 8125
	$(VERSIONBUMP)
	$(CMAKE_BUILD)
	$(CMAKE_ALL)
	$(RUN_SERVER)

server: stop_port cmake
	$(CMAKE_ALL) --target ogg.server
	$(RUN_SERVER)

client: cmake
	$(CMAKE_ALL) --target ogg.client

# Local Windows release zip (same layout as CI ogg-windows-v*.zip).
release: build
	@VERSION=$$(tr -d '\r\n' < $(VERSION_FILE)); \
	PKG=ogg-windows-$$VERSION; \
	rm -rf dist/$$PKG dist/$$PKG.zip; \
	mkdir -p dist/$$PKG; \
	cp build/ogg.launcher.exe build/ogg.server.exe build/WebView2Loader.dll dist/$$PKG/; \
	cp build/ogg.client.*.exe build/ogg.client.*.patch dist/$$PKG/; \
	if [ -d build/public_html ]; then cp -r build/public_html dist/$$PKG/public_html; \
	else cp -r src/OffGridGamer.OffGridGames.Web/public_html dist/$$PKG/public_html; fi; \
	cd dist && tar -acf $$PKG.zip $$PKG; \
	echo "Created dist/$$PKG.zip"

launcher: cmake
	$(CMAKE_ALL) --target ogg.launcher ogg.client
	-$(RUN_LAUNCHER)

reload:
	$(RUN_SERVER) -s reload

clean:
	-cmd //c "if exist build rmdir /s /q build"
	-rm -rf build
