.PHONY: clean build server cmake launcher icon hero version stop_port tools client release run_client

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
HEROGEN := build/tools/hero/ogg.hero.exe
HERO_ART := src/OffGridGamer.OffGridGames.Client/hero_art.jpg
RUN_SERVER := build\ogg.server.exe
RUN_LAUNCHER := build\ogg.launcher.exe
RUN_CLIENT_URL := http://127.0.0.1:8123/client
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

# Download abstract art, cover-crop to client hero panel size (832x648), write hero_art.jpg.
hero: cmake
	$(CMAKE_ALL) --target ogg.hero
	$(HEROGEN) $(HERO_ART)

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

# Build and launch the client with the local server URL (port 8123).
run_client: client
	@VERSION=$$(tr -d '\r\n' < $(VERSION_FILE)); \
	echo "Launching build/ogg.client.$$VERSION.exe $(RUN_CLIENT_URL)"; \
	build/ogg.client.$$VERSION.exe "$(RUN_CLIENT_URL)" &

# Local Windows release zip (same layout as CI ogg-windows-v*.zip).
release: build
	@VERSION=$$(tr -d '\r\n' < $(VERSION_FILE)); \
	PKG=ogg-windows-$$VERSION; \
	rm -rf dist/$$PKG dist/$$PKG.zip; \
	mkdir -p dist/$$PKG; \
	cp build/ogg.launcher.exe build/ogg.server.exe dist/$$PKG/; \
	cp build/ogg.client.*.exe build/ogg.client.*.patch build/client_login.xml dist/$$PKG/; \
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
