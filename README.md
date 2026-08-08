# OGG (OffGrid Games)

Native C++ game platform: Windows launcher + WebView2 client, cross-platform HTTP server, and web UI.

**Version:** v1.0.17

## Downloads

Pre-built **Release** binaries are published on GitHub Releases.

| Platform | Package | Contents |
|----------|---------|----------|
| **Windows (x64)** | [ogg-windows-v1.0.17.zip](https://github.com/papaya-hub/OGG/releases/download/v1.0.17/ogg-windows-v1.0.17.zip) | Launcher, client, server, WebView2 loader, web assets |
| **Linux (x64)** | [ogg-linux-v1.0.17.zip](https://github.com/papaya-hub/OGG/releases/download/v1.0.17/ogg-linux-v1.0.17.zip) | Server, client stub, web assets |

All releases: [github.com/papaya-hub/OGG/releases](https://github.com/papaya-hub/OGG/releases)

### Windows quick start

1. Download and unzip `ogg-windows-v*.zip`.
2. Run `ogg.launcher.exe` (downloads/updates the client from your server).
3. Or run the server: `ogg.server.exe` (serves `public_html` on port 8123).
4. **WebView2 Runtime** (Evergreen) is required for the GUI client — [install from Microsoft](https://developer.microsoft.com/microsoft-edge/webview2/).

### Linux quick start

1. Download and unzip `ogg-linux-v*.zip`.
2. Run `./ogg.server` from the extracted folder (web assets in `public_html/`).
3. The Linux `ogg.client` binary is a stub; use the web UI or a Windows client for the full GUI.

## Build from source

Requires **CMake 3.15+**, **Make**, and a C++23 compiler.

**Windows (MinGW UCRT):**

```cmd
make build
make server
make launcher
make client
```

**Linux:**

```sh
make build
make server
```

See [AGENTS.md](AGENTS.md) for project layout and contributor rules.

## License

See repository license file.
