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
#include <string>

#include "webview_host.hpp"
#include "widgets.hpp"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace ogg::ui {

namespace {

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

HMODULE load_webview2_loader() {
    wchar_t exe_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    PathRemoveFileSpecW(exe_path);

    wchar_t loader_path[MAX_PATH]{};
    PathCombineW(loader_path, exe_path, L"WebView2Loader.dll");
    HMODULE mod = LoadLibraryW(loader_path);
    if (mod) return mod;
    return LoadLibraryW(L"WebView2Loader.dll");
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

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return false;
    }
    g_webview.com_initialized = true;

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
