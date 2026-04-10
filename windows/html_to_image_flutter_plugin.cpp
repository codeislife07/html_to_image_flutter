// windows/html_to_image_flutter_plugin.cpp
//
// Windows WebView2 implementation of html_to_image_flutter.
//
// Dependencies (standard Windows SDK only):
//   <windows.h>, <objbase.h>, <wrl.h>  — always available in MSVC
//   "WebView2.h"                        — bundled next to this file
//
// No wil/com.h needed: uses Microsoft::WRL::ComPtr throughout.

#include "html_to_image_flutter_plugin.h"

#include <windows.h>
#include <objbase.h>     // CreateStreamOnHGlobal, IStream
#include <wrl.h>         // Microsoft::WRL::ComPtr, Callback
#include "WebView2.h"    // bundled: windows/WebView2.h + WebView2EnvironmentOptions.h

#include <memory>
#include <queue>
#include <string>
#include <vector>

using namespace Microsoft::WRL;

namespace html_to_image_flutter {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

// ExecuteScript returns the JS return value as JSON.
// `return "WxH"` (a JS string) becomes the LPCWSTR `"\"WxH\""`.
// Strip outer quotes, unescape, split on 'x'.
static std::pair<int,int> ParseWxH(LPCWSTR raw) {
    if (!raw) return {0, 0};
    std::wstring s(raw);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
        std::wstring u; u.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i)
            u += (s[i] == L'\\' && i + 1 < s.size()) ? s[++i] : s[i];
        s = u;
    }
    size_t x = s.find(L'x');
    if (x == std::wstring::npos) return {0, 0};
    try { return {std::stoi(s.substr(0, x)), std::stoi(s.substr(x + 1))}; }
    catch (...) { return {0, 0}; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Module-level globals
// ─────────────────────────────────────────────────────────────────────────────

struct PendingReq {
    std::string html;
    int         width;
    double      delay_ms;
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result;
};

static ComPtr<ICoreWebView2Environment> g_env;
static bool  g_env_creating = false;
static bool  g_busy         = false;
static HWND  g_hwnd         = nullptr;
static std::queue<PendingReq> g_queue;

static void ProcessQueue();  // forward declaration

// ─────────────────────────────────────────────────────────────────────────────
// Off-screen parent window
//
// WebView2's CapturePreview requires put_IsVisible(TRUE) on the controller.
// The parent HWND does NOT need to be on-screen; we create it hidden (no
// WS_VISIBLE) so the user never sees it, then set controller IsVisible=TRUE
// so the WebView2 COM layer actually renders the content.
// ─────────────────────────────────────────────────────────────────────────────

static HWND EnsureWindow() {
    if (g_hwnd) return g_hwnd;
    const wchar_t* cls = L"HtmlToImgFlutterWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);
    // WS_POPUP without WS_VISIBLE — window is created hidden (not shown to user).
    // Positioned off-screen as an extra safety measure.
    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        cls, L"",
        WS_POPUP,
        -32000, -32000, 800, 600,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    return g_hwnd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-request state — kept alive via shared_ptr captured in all callbacks
// ─────────────────────────────────────────────────────────────────────────────

struct State {
    ComPtr<ICoreWebView2Controller> ctrl;
    ComPtr<ICoreWebView2>           wv;
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result;
    int width_hint = 0;
    EventRegistrationToken nav_tok{};
    EventRegistrationToken msg_tok{};
};

static void Finish(std::shared_ptr<State> st, std::vector<uint8_t> bytes) {
    if (st->nav_tok.value) {
        st->wv->remove_NavigationCompleted(st->nav_tok);
        st->nav_tok = {};
    }
    if (st->msg_tok.value) {
        st->wv->remove_WebMessageReceived(st->msg_tok);
        st->msg_tok = {};
    }
    if (st->ctrl) {
        st->ctrl->Close();
        st->ctrl = nullptr;
        st->wv   = nullptr;
    }
    st->result->Success(flutter::EncodableValue(std::move(bytes)));
    g_busy = false;
    ProcessQueue();
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 — Measure content, set exact bounds, flush layout, CapturePreview
// ─────────────────────────────────────────────────────────────────────────────

static void Capture(std::shared_ptr<State> st) {
    // Measure actual scroll dimensions of the rendered content
    static const wchar_t* kMeasure =
        L"(function(){"
        L"var b=document.body,d=document.documentElement;"
        L"var w=Math.max(b?b.scrollWidth:0,d?d.scrollWidth:0,100);"
        L"var h=Math.max(b?b.scrollHeight:0,d?d.scrollHeight:0,100);"
        L"return String(w)+'x'+String(h);"
        L"})()";

    st->wv->ExecuteScript(kMeasure,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [st](HRESULT hr, LPCWSTR res) -> HRESULT {
            int w = st->width_hint > 0 ? st->width_hint : 800;
            int h = 600;
            if (SUCCEEDED(hr)) {
                auto [pw, ph] = ParseWxH(res);
                if (st->width_hint <= 0 && pw > 0) w = pw;
                if (ph > 0) h = ph;
            }

            // Apply exact content dimensions
            st->ctrl->put_Bounds(RECT{0, 0, w, h});

            // Execute a trivial script to flush the layout engine so the WebView
            // renders at the new bounds before CapturePreview fires.
            st->wv->ExecuteScript(L"document.body.offsetHeight",
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [st](HRESULT, LPCWSTR) -> HRESULT {
                    // Allocate an in-memory IStream to receive the PNG bytes
                    IStream* rawStream = nullptr;
                    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &rawStream))
                            || !rawStream) {
                        Finish(st, {});
                        return S_OK;
                    }
                    ComPtr<IStream> stream;
                    stream.Attach(rawStream);  // take ownership (no extra AddRef)

                    // CapturePreview writes PNG into stream.
                    // The inner lambda captures `stream` by value → ComPtr copy →
                    // AddRef → stream stays alive until callback fires.
                    st->wv->CapturePreview(
                        COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
                        stream.Get(),
                        Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                        [st, stream](HRESULT hr) mutable -> HRESULT {
                            std::vector<uint8_t> bytes;
                            if (SUCCEEDED(hr)) {
                                STATSTG stat{};
                                stream->Stat(&stat, STATFLAG_NONAME);
                                ULONG sz = stat.cbSize.LowPart;
                                if (sz > 0) {
                                    LARGE_INTEGER zero{};
                                    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
                                    bytes.resize(sz);
                                    ULONG rd = 0;
                                    stream->Read(bytes.data(), sz, &rd);
                                    if (rd != sz) bytes.resize(rd);
                                }
                            }
                            Finish(st, std::move(bytes));
                            return S_OK;
                        }).Get());

                    return S_OK;
                }).Get());

            return S_OK;
        }).Get());
}

// ─────────────────────────────────────────────────────────────────────────────
// Steps 1-2 — Create controller → navigate → on completion → capture
// ─────────────────────────────────────────────────────────────────────────────

static void StartConversion(PendingReq req) {
    g_busy = true;

    auto st        = std::make_shared<State>();
    st->result     = std::move(req.result);
    st->width_hint = req.width;

    // Wrap the caller's HTML fragment in a minimal document shell
    std::string wrapped =
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>*{box-sizing:border-box;}html,body{margin:0;padding:0;}</style>"
        "</head><body>" + req.html + "</body></html>";

    std::wstring whtml       = ToWide(wrapped);
    double       delay_ms    = req.delay_ms;
    int          initWidth   = req.width > 0 ? req.width : 800;

    g_env->CreateCoreWebView2Controller(
        EnsureWindow(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [st, whtml, delay_ms, initWidth](
                HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
            if (FAILED(hr) || !ctrl) {
                st->result->Error("CTRL_ERR",
                    "WebView2 controller creation failed");
                g_busy = false;
                ProcessQueue();
                return S_OK;
            }

            st->ctrl = ctrl;
            st->ctrl->get_CoreWebView2(st->wv.ReleaseAndGetAddressOf());
            if (!st->wv) {
                st->result->Error("WV_ERR", "get_CoreWebView2 returned null");
                st->ctrl->Close();
                g_busy = false;
                ProcessQueue();
                return S_OK;
            }

            // Set initial bounds wide enough and tall enough for any receipt.
            // Width is fixed (or will be measured); height 8000px covers all cases.
            st->ctrl->put_Bounds(RECT{0, 0, initWidth, 8000});

            // *** CRITICAL: IsVisible must be TRUE for CapturePreview to render
            // content rather than returning a blank white image.
            // The parent HWND is hidden from the user; this flag only controls
            // whether the WebView2 compositor renders internally. ***
            st->ctrl->put_IsVisible(TRUE);

            // One-shot navigation-completed handler
            st->wv->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [st, delay_ms](ICoreWebView2*,
                               ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                    st->wv->remove_NavigationCompleted(st->nav_tok);
                    st->nav_tok = {};

                    if (delay_ms <= 0.0) {
                        Capture(st);
                    } else {
                        // Yield back to the message loop for `delay_ms` ms via
                        // JS setTimeout, then signal via postMessage so we don't
                        // block the UI thread with sleep.
                        st->wv->add_WebMessageReceived(
                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                            [st](ICoreWebView2*,
                                 ICoreWebView2WebMessageReceivedEventArgs*) -> HRESULT {
                                st->wv->remove_WebMessageReceived(st->msg_tok);
                                st->msg_tok = {};
                                Capture(st);
                                return S_OK;
                            }).Get(), &st->msg_tok);

                        std::wstring script =
                            L"setTimeout(function(){"
                            L"window.chrome.webview.postMessage('go');"
                            L"}," + std::to_wstring(static_cast<int>(delay_ms)) + L");";
                        st->wv->ExecuteScript(script.c_str(), nullptr);
                    }
                    return S_OK;
                }).Get(), &st->nav_tok);

            st->wv->NavigateToString(whtml.c_str());
            return S_OK;
        }).Get());
}

static void ProcessQueue() {
    if (g_busy || g_queue.empty()) return;
    auto req = std::move(g_queue.front());
    g_queue.pop();
    StartConversion(std::move(req));
}

// ─────────────────────────────────────────────────────────────────────────────
// Plugin boilerplate
// ─────────────────────────────────────────────────────────────────────────────

HtmlToImageFlutterPlugin::HtmlToImageFlutterPlugin()  = default;
HtmlToImageFlutterPlugin::~HtmlToImageFlutterPlugin() = default;

void HtmlToImageFlutterPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
    auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            registrar->messenger(), "html_to_image_flutter",
            &flutter::StandardMethodCodec::GetInstance());
    auto plugin = std::make_unique<HtmlToImageFlutterPlugin>();
    channel->SetMethodCallHandler(
        [p = plugin.get()](const auto& call, auto result) {
            p->HandleMethodCall(call, std::move(result));
        });
    registrar->AddPlugin(std::move(plugin));
}

void HtmlToImageFlutterPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    if (call.method_name() != "convertToImage") {
        result->NotImplemented();
        return;
    }

    const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
    if (!args) {
        result->Error("INVALID_ARGS", "Missing arguments");
        return;
    }

    // ── 'content' (required) ────────────────────────────────────────────────
    auto ci = args->find(flutter::EncodableValue("content"));
    if (ci == args->end() ||
        !std::holds_alternative<std::string>(ci->second)) {
        result->Error("BAD_ARGS", "Missing or invalid 'content'");
        return;
    }
    std::string html = std::get<std::string>(ci->second);

    // ── 'delay' in ms (optional, default 200) ───────────────────────────────
    double delay_ms = 200.0;
    auto di = args->find(flutter::EncodableValue("delay"));
    if (di != args->end()) {
        if (const auto* d = std::get_if<double>(&di->second)) delay_ms = *d;
        else if (const auto* i = std::get_if<int>(&di->second))
            delay_ms = static_cast<double>(*i);
    }

    // ── 'width' in px (optional, 0 = auto-measure from content) ─────────────
    int width = 0;
    auto wi = args->find(flutter::EncodableValue("width"));
    if (wi != args->end()) {
        if (const auto* i = std::get_if<int>(&wi->second)) width = *i;
        else if (const auto* d = std::get_if<double>(&wi->second))
            width = static_cast<int>(*d);
    }

    PendingReq req;
    req.html     = std::move(html);
    req.delay_ms = delay_ms;
    req.width    = width;
    req.result   = std::move(result);

    if (!g_env) {
        // Queue first; start environment creation once
        g_queue.push(std::move(req));
        if (!g_env_creating) {
            g_env_creating = true;
            EnsureWindow();
            CreateCoreWebView2EnvironmentWithOptions(
                nullptr, nullptr, nullptr,
                Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                    g_env_creating = false;
                    if (SUCCEEDED(hr) && env) {
                        g_env = env;
                        ProcessQueue();
                    } else {
                        // Drain queue with error
                        while (!g_queue.empty()) {
                            g_queue.front().result->Error(
                                "ENV_ERR",
                                "WebView2 environment creation failed. "
                                "Install the WebView2 Runtime from Microsoft.");
                            g_queue.pop();
                        }
                    }
                    return S_OK;
                }).Get());
        }
    } else if (g_busy) {
        g_queue.push(std::move(req));
    } else {
        StartConversion(std::move(req));
    }
}

}  // namespace html_to_image_flutter
