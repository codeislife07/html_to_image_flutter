// windows/html_to_image_flutter_plugin.cpp
#include "html_to_image_flutter_plugin.h"

#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <WebView2.h>

#include <chrono>
#include <thread>

using namespace Microsoft::WRL;

static wil::com_ptr<ICreateWebView2Environment> g_env;
static HWND g_hidden_hwnd = nullptr;

// ---------------------------------------------------------------------------
// Helper: create a hidden window (required for WebView2)
static HWND CreateHiddenWindow() {
  const wchar_t CLASS_NAME[] = L"FlutterHtmlToImageHiddenWindow";

  WNDCLASS wc = {};
  wc.lpfnWndProc = DefWindowProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = CLASS_NAME;
  RegisterClass(&wc);

  return CreateWindowEx(0, CLASS_NAME, L"", 0, 0, 0, 1, 1,
                        HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);
}

// ---------------------------------------------------------------------------
// Plugin registration
void HtmlToImageFlutterPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar *registrar) {
  auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
      registrar->messenger(), "html_to_image_flutter",
      &flutter::StandardMethodCodec::GetInstance());

  auto plugin = std::make_unique<HtmlToImageFlutterPlugin>();

  channel->SetMethodCallHandler(
      [plugin_ptr = plugin.get()](const auto &call, auto result) {
        plugin_ptr->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

// ---------------------------------------------------------------------------
// Method handling
void HtmlToImageFlutterPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  if (call.method_name() != "convertToImage") {
    result->NotImplemented();
    return;
  }

  const auto *args = std::get_if<flutter::EncodableMap>(call.arguments());
  if (!args) {
    result->Error("INVALID_ARGUMENTS", "Missing arguments");
    return;
  }

  ConvertToImage(*args, std::move(result));
}

// ---------------------------------------------------------------------------
// Core conversion
void HtmlToImageFlutterPlugin::ConvertToImage(
    const flutter::EncodableMap &args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

  auto content_it = args.find(flutter::EncodableValue("content"));
  auto delay_it = args.find(flutter::EncodableValue("delay"));
  if (content_it == args.end() || !std::holds_alternative<std::string>(content_it->second)) {
    result->Error("BAD_ARGS", "Missing or invalid 'content'");
    return;
  }

  std::string html = std::get<std::string>(content_it->second);
  double delay_ms = delay_it != args.end() ? std::get<double>(delay_it->second) : 200.0;

  // -----------------------------------------------------------------------
  // 1. Prepare hidden window + WebView2 environment
  if (!g_hidden_hwnd) g_hidden_hwnd = CreateHiddenWindow();

  // Create environment once
  if (!g_env) {
    CreateWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICreateWebView2EnvironmentCompletedHandler>(
            <a href="HRESULT, IWebView2Environment *env" target="_blank" rel="noopener noreferrer nofollow"></a> {
              g_env = env;
              return S_OK;
            }).Get());
    // Wait until env is ready (simple spin-loop)
    while (!g_env) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // -----------------------------------------------------------------------
  // 2. Create WebView
  wil::com_ptr<ICoreWebView2> webview;
  g_env->CreateCoreWebView2Host(g_hidden_hwnd, nullptr,
      Callback<ICoreWebView2CreateCoreWebView2HostCompletedHandler>(
          [&](HRESULT, ICoreWebView2Host *host) {
            host->get_CoreWebView2(&webview);
            return S_OK;
          }).Get());

  while (!webview) std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // -----------------------------------------------------------------------
  // 3. Load HTML (with viewport meta)
  std::string wrapped = R"(
    <!DOCTYPE html><html><head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>body{margin:0;padding:0;}</style>
    </head><body>)" + html + R"(</body></html>)";

  webview->NavigateToString(std::wstring(wrapped.begin(), wrapped.end()).c_str());

  // -----------------------------------------------------------------------
  // 4. Wait for navigation + optional JS delay
  bool navigation_done = false;
  EventRegistrationToken nav_token;
  webview->add_NavigationCompleted(
      Callback<ICoreWebView2NavigationCompletedEventHandler>(
          [&](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) {
            BOOL success;
            args->get_IsSuccess(&success);
            navigation_done = true;
            return S_OK;
          }).Get(),
      &nav_token);

  while (!navigation_done) std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(delay_ms)));

  // -----------------------------------------------------------------------
  // 5. Capture full page as PNG
  webview->CallMethodOnMainThread([webview, result = std::move(result)]() mutable {
    // Get content size
    wil::com_ptr<ICoreWebView2_13> webview13;
    webview->QueryInterface(IID_PPV_ARGS(&webview13));

    if (!webview13) {
      result->Success(flutter::EncodableValue(std::vector<uint8_t>{}));
      return;
    }

    // Execute script to obtain scrollWidth/scrollHeight
    webview->ExecuteScript(LR"(
      (function(){
        const w = Math.max(document.documentElement.scrollWidth, document.body.scrollWidth);
        const h = Math.max(document.documentElement.scrollHeight, document.body.scrollHeight);
        window.chrome.webview.postMessage(JSON.stringify({w,h}));
      })();
    )", Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [&](HRESULT, LPCWSTR res) {
          std::wstring wres(res);
          // Parse JSON {"w":…, "h":…}
          int width = 0, height = 0;
          // simple parse (you can use nlohmann/json if you like)
          size_t pos = wres.find(L"\"w\":");
          if (pos != std::wstring::npos) {
            pos = wres.find_first_of(L"0123456789", pos + 4);
            width = std::stoi(wres.substr(pos));
          }
          pos = wres.find(L"\"h\":");
          if (pos != std::wstring::npos) {
            pos = wres.find_first_of(L"0123456789", pos + 4);
            height = std::stoi(wres.substr(pos));
          }

          if (width <= 0 || height <= 0) {
            result->Success(flutter::EncodableValue(std::vector<uint8_t>{}));
            return;
          }

          // Resize the WebView to the content size
          wil::com_ptr<ICoreWebView2Controller> controller;
          webview->get_Controller(&controller);
          controller->put_Bounds(RECT{0, 0, width, height});

          // Capture
          webview13->CapturePreview(
              COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
              nullptr, // we use a memory stream later
              Callback<ICoreWebView2CapturePreviewCompletedHandler>(
                  [&](HRESULT) {
                    // Get the PNG bytes via GetCoreWebView2Content
                    wil::com_ptr<IStream> stream;
                    webview13->GetCoreWebView2Content(&stream);

                    STATSTG stat;
                    stream->Stat(&stat, STATFLAG_NONAME);
                    std::vector<uint8_t> png_bytes(stat.cbSize.LowPart);
                    ULONG read;
                    stream->Read(png_bytes.data(), static_cast<ULONG>(png_bytes.size()), &read);

                    result->Success(flutter::EncodableValue(png_bytes));
                    return S_OK;
                  }).Get());

          return S_OK;
        }).Get());

    return S_OK;
  });
}