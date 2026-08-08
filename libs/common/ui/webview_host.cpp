#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shlwapi.h>
#include <WebView2.h>

#include <functional>
#include <cstring>
#include <string>

#include "webview_host.hpp"
#include "widgets.hpp"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace ogg::ui {

namespace {

constexpr int kWebViewLoaderResourceId = 101;
constexpr wchar_t kWebViewLoaderDllName[] = L"WebView2Loader.dll";

using LoadedHandler = std::function<void()>;

class EnvHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    explicit EnvHandler(std::function<void(HRESULT, ICoreWebView2Environment*)> fn) : fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(riid, IID_IUnknown) ||
            IsEqualGUID(riid, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG c = --refs_;
        if (c == 0) delete this;
        return c;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT error_code, ICoreWebView2Environment* env) override {
        if (fn_) fn_(error_code, env);
        return S_OK;
    }

private:
    ULONG refs_ = 1;
    std::function<void(HRESULT, ICoreWebView2Environment*)> fn_;
};

class ControllerHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    explicit ControllerHandler(std::function<void(HRESULT, ICoreWebView2Controller*)> fn) : fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(riid, IID_IUnknown) ||
            IsEqualGUID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG c = --refs_;
        if (c == 0) delete this;
        return c;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT error_code, ICoreWebView2Controller* controller) override {
        if (fn_) fn_(error_code, controller);
        return S_OK;
    }

private:
    ULONG refs_ = 1;
    std::function<void(HRESULT, ICoreWebView2Controller*)> fn_;
};

class NavigationHandler final : public ICoreWebView2NavigationCompletedEventHandler {
public:
    explicit NavigationHandler(std::function<void(ICoreWebView2NavigationCompletedEventArgs*)> fn) : fn_(std::move(fn)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(riid, IID_IUnknown) ||
            IsEqualGUID(riid, IID_ICoreWebView2NavigationCompletedEventHandler)) {
            *ppv = static_cast<ICoreWebView2NavigationCompletedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG c = --refs_;
        if (c == 0) delete this;
        return c;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) override {
        if (fn_) fn_(args);
        return S_OK;
    }

private:
    ULONG refs_ = 1;
    std::function<void(ICoreWebView2NavigationCompletedEventArgs*)> fn_;
};

struct WebViewState {
    HWND parent = nullptr;
    std::wstring url;
    LoadedHandler on_loaded;
    bool com_initialized = false;
    ICoreWebView2Environment* environment = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* webview = nullptr;
    EventRegistrationToken navigation_token{};
};

WebViewState g_webview{};

bool extract_resource_to_file(int resource_id, const wchar_t* destination) {
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) return false;

    const HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) return false;

    const void* data = LockResource(loaded);
    const DWORD size = SizeofResource(nullptr, resource);
    if (!data || size == 0) return false;

    HANDLE file = CreateFileW(
        destination,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    return ok;
}

bool ensure_webview_loader_dll(wchar_t* loader_path, int loader_path_capacity) {
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return false;

    wchar_t exe_dir[MAX_PATH]{};
    wcsncpy(exe_dir, exe_path, MAX_PATH - 1);
    exe_dir[MAX_PATH - 1] = L'\0';
    PathRemoveFileSpecW(exe_dir);

    if (PathCombineW(loader_path, exe_dir, kWebViewLoaderDllName) == nullptr) return false;
    if (static_cast<int>(wcslen(loader_path)) >= loader_path_capacity) return false;

    if (GetFileAttributesW(loader_path) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    return extract_resource_to_file(kWebViewLoaderResourceId, loader_path);
}

HMODULE load_webview2_loader() {
    wchar_t loader_path[MAX_PATH]{};
    if (!ensure_webview_loader_dll(loader_path, MAX_PATH)) {
        return LoadLibraryW(kWebViewLoaderDllName);
    }

    HMODULE mod = LoadLibraryW(loader_path);
    if (mod) return mod;
    return LoadLibraryW(kWebViewLoaderDllName);
}

void layout_bounds(HWND parent, RECT& bounds) {
    GetClientRect(parent, &bounds);
}

} // namespace

bool embed_webview(HWND parent, const std::wstring& url, LoadedHandler on_loaded) {
    if (!parent || url.empty()) return false;

    destroy_embedded_webview();

    g_webview.parent = parent;
    g_webview.url = url;
    g_webview.on_loaded = std::move(on_loaded);

    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (com_hr == RPC_E_CHANGED_MODE) {
        g_webview.com_initialized = false;
    } else if (FAILED(com_hr)) {
        return false;
    } else {
        g_webview.com_initialized = true;
    }

    HMODULE loader = load_webview2_loader();
    if (!loader) return false;

    auto create_env = reinterpret_cast<decltype(&CreateCoreWebView2EnvironmentWithOptions)>(
        GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions")
    );
    if (!create_env) return false;

    auto* env_handler = new EnvHandler([](HRESULT result, ICoreWebView2Environment* env) {
        if (FAILED(result) || !env) return;
        g_webview.environment = env;
        env->AddRef();

        auto* controller_handler = new ControllerHandler([](HRESULT controller_result, ICoreWebView2Controller* controller) {
            if (FAILED(controller_result) || !controller) return;
            g_webview.controller = controller;
            controller->AddRef();

            if (FAILED(controller->get_CoreWebView2(&g_webview.webview)) || !g_webview.webview) return;

            ICoreWebView2Settings* settings = nullptr;
            if (SUCCEEDED(g_webview.webview->get_Settings(&settings)) && settings) {
                settings->put_AreDefaultContextMenusEnabled(FALSE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->Release();
            }

            g_webview.webview->AddScriptToExecuteOnDocumentCreated(
                LR"JS((function(){
  var s=document.createElement('style');
  s.textContent='*{user-select:none;-webkit-user-select:none;}input,textarea,[contenteditable=true]{user-select:text;-webkit-user-select:text;}';
  document.documentElement.appendChild(s);
  document.addEventListener('selectstart',function(e){
    var t=e.target;
    if(t&&(t.tagName==='INPUT'||t.tagName==='TEXTAREA'||t.isContentEditable))return;
    e.preventDefault();
  },true);
})();)JS",
                nullptr
            );

            RECT bounds{};
            layout_bounds(g_webview.parent, bounds);
            controller->put_Bounds(bounds);
            controller->put_IsVisible(FALSE);

            auto* nav_handler = new NavigationHandler([](ICoreWebView2NavigationCompletedEventArgs* args) {
                BOOL success = FALSE;
                if (args) args->get_IsSuccess(&success);
                if (success) {
                    if (g_webview.controller) g_webview.controller->put_IsVisible(TRUE);
                    if (g_webview.on_loaded) g_webview.on_loaded();
                }
            });

            g_webview.webview->add_NavigationCompleted(nav_handler, &g_webview.navigation_token);
            nav_handler->Release();
            g_webview.webview->Navigate(g_webview.url.c_str());
        });

        g_webview.environment->CreateCoreWebView2Controller(g_webview.parent, controller_handler);
        controller_handler->Release();
    });

    const HRESULT hr = create_env(nullptr, nullptr, nullptr, env_handler);
    env_handler->Release();
    return SUCCEEDED(hr);
}

void set_embedded_webview_bounds(HWND parent, int x, int y, int width, int height) {
    if (!g_webview.controller || parent != g_webview.parent) return;
    if (width <= 0 || height <= 0) return;
    const RECT bounds{ x, y, x + width, y + height };
    g_webview.controller->put_Bounds(bounds);
}

void layout_embedded_webview(HWND parent) {
    if (!g_webview.controller || parent != g_webview.parent) return;
    RECT bounds{};
    layout_bounds(parent, bounds);
    g_webview.controller->put_Bounds(bounds);
}

void destroy_embedded_webview() {
    if (g_webview.webview) {
        g_webview.webview->Release();
        g_webview.webview = nullptr;
    }
    if (g_webview.controller) {
        g_webview.controller->Release();
        g_webview.controller = nullptr;
    }
    if (g_webview.environment) {
        g_webview.environment->Release();
        g_webview.environment = nullptr;
    }
    if (g_webview.com_initialized) {
        CoUninitialize();
        g_webview.com_initialized = false;
    }
    g_webview.parent = nullptr;
    g_webview.url.clear();
    g_webview.on_loaded = nullptr;
}

} // namespace ogg::ui

#endif
