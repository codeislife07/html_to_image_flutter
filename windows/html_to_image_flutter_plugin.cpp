// windows/html_to_image_flutter_plugin.cpp
//
// Windows implementation of html_to_image_flutter using WebView2.
// All WebView2 operations run on the Flutter UI thread (which already has a
// message pump), so callbacks are dispatched without any blocking spin-loops.
// Requests are processed sequentially: one WebView2 controller per request,
// destroyed after the PNG bytes are captured.

#include "html_to_image_flutter_plugin.h"

// Order matters: windows.h first, then wrl/wil, then WebView2
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <wrl.h>
#include "wil/com.h"
#include "WebView2.h"

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

// ExecuteScript returns the JS value as JSON. A string `return "WxH"` becomes
// the LPCWSTR `"\"WxH\""`. Strip outer quotes and unescape, then split on 'x'.
static std::pair<int, int> ParseWxH(LPCWSTR raw) {
    if (!raw) return {0, 0};
    std::wstring s(raw);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
        std::wstring u;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\\' && i + 1 < s.size()) { u += s[++i]; }
            else                                    { u += s[i];   }
        }
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

static wil::com_ptr<ICoreWebView2Environment> g_env;
static bool  g_env_creating = false;
static bool  g_busy         = false;
static HWND  g_hwnd         = nullptr;
static std::queue<PendingReq> g_queue;

static void ProcessQueue();  // forward declaration

// ─────────────────────────────────────────────────────────────────────────────
// Hidden parent window (required by WebView2 CreateCoreWebView2Controller)
// ─────────────────────────────────────────────────────────────────────────────

static HWND EnsureHiddenWindow() {
    if (g_hwnd) return g_hwnd;
    const wchar_t* cls = L"HtmlToImgFlutterWnd";
    WNDCLASSW wc{};
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassW(&wc);
    // No WS_VISIBLE → hidden by default.
    g_hwnd = CreateWindowExW(0, cls, L"", WS_OVERLAPPED,
                             0, 0, 1, 1, nullptr, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    return g_hwnd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-request state (kept alive via shared_ptr captured in callbacks)
// ─────────────────────────────────────────────────────────────────────────────

struct State {
    wil::com_ptr<ICoreWebView2Controller> ctrl;
    wil::com_ptr<ICoreWebView2>           wv;
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result;
    int width_hint = 0;
    EventRegistrationToken nav_tok{};
    EventRegistrationToken msg_tok{};
};

// Release WebView2 resources, return bytes to Flutter, process next queued item.
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
// Step 3 – Measure content, resize, capture PNG
// ─────────────────────────────────────────────────────────────────────────────

static void Capture(std::shared_ptr<State> st) {
    // Get scroll dimensions via JavaScript
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

            // Resize WebView to the content's natural dimensions
            st->ctrl->put_Bounds(RECT{0, 0, w, h});

            // Allocate an in-memory IStream to receive the PNG
            wil::com_ptr<IStream> stream;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()))) {
                Finish(st, {});
                return S_OK;
            }

            // CapturePreview writes PNG bytes into `stream`; callback fires
            // on the UI thread once the capture is complete.
            st->wv->CapturePreview(
                COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
                stream.get(),
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
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 1-2 – Create controller, navigate, wait for navigation + optional delay
// ─────────────────────────────────────────────────────────────────────────────

static void StartConversion(PendingReq req) {
    g_busy = true;

    auto st        = std::make_shared<State>();
    st->result     = std::move(req.result);
    st->width_hint = req.width;

    // Wrap with minimal HTML boilerplate for correct rendering
    std::string wrapped =
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<style>*{box-sizing:border-box;}html,body{margin:0;padding:0;}</style>"
        "</head><body>" + req.html + "</body></html>";

    std::wstring whtml     = ToWide(wrapped);
    double       delay_ms  = req.delay_ms;

    // Create an off-screen WebView2 controller bound to the hidden window
    g_env->CreateCoreWebView2Controller(
        EnsureHiddenWindow(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [st, whtml, delay_ms](HRESULT hr,
                              ICoreWebView2Controller* ctrl) -> HRESULT {
            if (FAILED(hr) || !ctrl) {
                st->result->Error("CTRL_ERR",
                    "WebView2 controller creation failed");
                g_busy = false;
                ProcessQueue();
                return S_OK;
            }

            st->ctrl = ctrl;
            st->ctrl->get_CoreWebView2(st->wv.put());

            if (!st->wv) {
                st->result->Error("WV_ERR", "get_CoreWebView2 failed");
                st->ctrl->Close();
                g_busy = false;
                ProcessQueue();
                return S_OK;
            }

            // Keep the WebView off-screen; give it initial bounds
            st->ctrl->put_IsVisible(FALSE);
            st->ctrl->put_Bounds(RECT{0, 0, 800, 600});

            // Register one-shot navigation-completed handler
            st->wv->add_NavigationCompleted(
                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [st, delay_ms](ICoreWebView2*,
                               ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                    st->wv->remove_NavigationCompleted(st->nav_tok);
                    st->nav_tok = {};

                    if (delay_ms <= 0.0) {
                        // No delay: go straight to capture
                        Capture(st);
                    } else {
                        // Use JS setTimeout + window.chrome.webview.postMessage
                        // so we yield back to the message loop during the delay.
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

            // Begin loading the HTML
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
        result->Error("INVALID_ARGS", "Missing arguments map");
        return;
    }

    // ── Extract 'content' ────────────────────────────────────────────────────
    auto ci = args->find(flutter::EncodableValue("content"));
    if (ci == args->end() ||
        !std::holds_alternative<std::string>(ci->second)) {
        result->Error("BAD_ARGS", "Missing or invalid 'content'");
        return;
    }
    std::string html = std::get<std::string>(ci->second);

    // ── Extract 'delay' (ms, default 200) ───────────────────────────────────
    double delay_ms = 200.0;
    auto di = args->find(flutter::EncodableValue("delay"));
    if (di != args->end()) {
        if (const auto* d = std::get_if<double>(&di->second)) delay_ms = *d;
        else if (const auto* i = std::get_if<int>(&di->second)) delay_ms = *i;
    }

    // ── Extract 'width' (px, 0 = measure from content) ──────────────────────
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
        // Environment not yet created – queue the request and start creation
        g_queue.push(std::move(req));

        if (!g_env_creating) {
            g_env_creating = true;
            EnsureHiddenWindow();

            CreateCoreWebView2EnvironmentWithOptions(
                nullptr, nullptr, nullptr,
                Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                    g_env_creating = false;
                    if (SUCCEEDED(hr) && env) {
                        g_env = env;
                        ProcessQueue();
                    } else {
                        // Fail all queued requests
                        while (!g_queue.empty()) {
                            g_queue.front().result->Error(
                                "ENV_ERR",
                                "WebView2 environment creation failed. "
                                "Ensure WebView2 Runtime is installed.");
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
