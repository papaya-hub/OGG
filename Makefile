.PHONY: clean build server cmake launcher icon hero version stop_port tools client admin release run_client sync_hero sync_settings test_ai_images test_all

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
SYNC_SETTINGS := build/tools/sync-settings/ogg.sync-settings.exe
TEST_AI_IMAGE := build/tools/test-ai-image/ogg.test-ai-image.exe
HERO_ART := src/OffGridGamer.OffGridGames.Client/hero_art.jpg
ifeq ($(OS),Windows_NT)
LOCALAPPDATA_WIN := $(shell cmd //c "echo %LOCALAPPDATA%" | tr -d '\r')
APPDATA_OGG := $(subst \,/,$(LOCALAPPDATA_WIN))/OffGridGames
LOGIN_IMAGES_DIR := $(APPDATA_OGG)/login_images
SELECTED_FILE := $(APPDATA_OGG)/selected_login_image.txt
else
APPDATA_OGG := $(HOME)/.local/share/OffGridGames
LOGIN_IMAGES_DIR := $(APPDATA_OGG)/login_images
SELECTED_FILE := $(APPDATA_OGG)/selected_login_image.txt
endif
RUN_SERVER := build\ogg.server.exe
RUN_LAUNCHER := build\ogg.launcher.exe
RUN_ADMIN := build\ogg.admin.exe
RUN_CLIENT_URL := http://127.0.0.1:8123/client
VERSION_FILE := src/version.txt

all: build

build: cmake sync_settings
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

# Download abstract art, cover-crop to client hero panel size (832x648), add to AppData gallery.
hero: cmake
	$(CMAKE_ALL) --target ogg.hero
	@mkdir -p $(LOGIN_IMAGES_DIR)
	@mkdir -p $(dir $(SELECTED_FILE))
	@STAMP=$$(date +%Y%m%d_%H%M%S); \
	FNAME=login_$$STAMP.jpg; \
	OUT=$(LOGIN_IMAGES_DIR)/$$FNAME; \
	$(HEROGEN) $$OUT; \
	printf '%s\n' $$FNAME > $(SELECTED_FILE); \
	cp $$OUT $(HERO_ART); \
	echo "hero: $$OUT (AppData gallery, selected for next make client)"

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

# Copy selected AppData login image into hero_art.jpg before client embed build.
sync_hero:
	@mkdir -p $(LOGIN_IMAGES_DIR)
	@mkdir -p $(dir $(SELECTED_FILE))
	@if [ -d "src/OffGridGamer.OffGridGames.Client/login_images" ]; then \
	  for f in src/OffGridGamer.OffGridGames.Client/login_images/*.jpg; do \
	    if [ -f "$$f" ]; then cp -n "$$f" "$(LOGIN_IMAGES_DIR)/" 2>/dev/null || true; fi; \
	  done; \
	fi
	@if [ -f "src/OffGridGamer.OffGridGames.Client/selected_login_image.txt" ] && [ ! -f "$(SELECTED_FILE)" ]; then \
	  cp "src/OffGridGamer.OffGridGames.Client/selected_login_image.txt" "$(SELECTED_FILE)"; \
	fi
	@if [ ! -f "$(HERO_ART)" ] && [ -d "$(LOGIN_IMAGES_DIR)" ]; then \
	  FIRST=$$(ls "$(LOGIN_IMAGES_DIR)"/*.jpg 2>/dev/null | head -n 1); \
	  if [ -n "$$FIRST" ]; then cp "$$FIRST" "$(HERO_ART)"; fi; \
	fi
	@if [ -f "$(SELECTED_FILE)" ]; then \
	  SEL=$$(tr -d '\r\n' < "$(SELECTED_FILE)"); \
	  if [ -n "$$SEL" ] && [ -f "$(LOGIN_IMAGES_DIR)/$$SEL" ]; then \
	    cp "$(LOGIN_IMAGES_DIR)/$$SEL" "$(HERO_ART)"; \
	    echo "sync_hero: $(HERO_ART) <- $(LOGIN_IMAGES_DIR)/$$SEL"; \
	  fi; \
	fi

sync_settings: cmake
	$(CMAKE_ALL) --target ogg.sync-settings
	-$(SYNC_SETTINGS)

# Generate sample login hero images via OpenAI + Gemini (keys from AppData settings).
test_ai_images: cmake
	$(CMAKE_ALL) --target ogg.test-ai-image
	@mkdir -p build/test_ai_images
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/test-ai-image/generate.ps1 -Prompt "five stax gaming party"

client: sync_hero sync_settings cmake
	$(CMAKE_ALL) --target ogg.client

# Build all shipped executables (no launch) — stops running OGG apps if they block the linker.
test_all: stop_port sync_settings cmake
ifeq ($(OS),Windows_NT)
	-cmd //c "taskkill /F /IM ogg.admin.exe /IM ogg.client.v1.0.20.exe /IM ogg.launcher.exe /IM ogg.server.exe >nul 2>&1"
endif
	$(CMAKE_ALL) --target ogg.server ogg.client ogg.launcher ogg.admin ogg.sync-settings

admin: sync_settings cmake
	$(CMAKE_ALL) --target ogg.admin
	-$(RUN_ADMIN)

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

launcher: sync_settings cmake
	$(CMAKE_ALL) --target ogg.launcher ogg.client
	-$(RUN_LAUNCHER)

reload:
	$(RUN_SERVER) -s reload

clean:
	-cmd //c "if exist build rmdir /s /q build"
	-rm -rf build
