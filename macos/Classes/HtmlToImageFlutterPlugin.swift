import Cocoa
import FlutterMacOS
import WebKit

public class HtmlToImageFlutterPlugin: NSObject, FlutterPlugin {
    private var webView: WKWebView!
    private var resultCallback: FlutterResult?

    public static func register(with registrar: FlutterPluginRegistrar) {
        let channel = FlutterMethodChannel(name: "html_to_image_flutter",
                                           binaryMessenger: registrar.messenger)
        let instance = HtmlToImageFlutterPlugin()
        registrar.addMethodCallDelegate(instance, channel: channel)
    }

    public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard call.method == "convertToImage",
              let args = call.arguments as? [String: Any],
              let content = args["content"] as? String else {
            result(FlutterMethodNotImplemented)
            return
        }

        let delay = (args["delay"] as? Double) ?? 200.0
        self.resultCallback = result

        // -----------------------------------------------------------------
        // 1. Create off-screen WKWebView
        let config = WKWebViewConfiguration()
        config.preferences.setValue(true, forKey: "developerExtrasEnabled")
        webView = WKWebView(frame: .zero, configuration: config)
        webView.navigationDelegate = self
        webView.isHidden = true

        // -----------------------------------------------------------------
        // 2. Wrap HTML with viewport meta (same as iOS)
        let wrappedHTML = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>body { margin:0; padding:0; }</style>
        </head>
        <body>
        \(content)
        </body>
        </html>
        """

        webView.loadHTMLString(wrappedHTML, baseURL: nil)

        // Store delay for later
        webView.customUserInfo = ["delay": delay]
    }

    // -----------------------------------------------------------------
    // Helper: clean up
    private func dispose() {
        webView?.navigationDelegate = nil
        webView?.removeFromSuperview()
        webView = nil
        resultCallback = nil
    }
}

// MARK: - WKNavigationDelegate
extension HtmlToImageFlutterPlugin: WKNavigationDelegate {
    public func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard let delay = webView.customUserInfo?["delay"] as? Double else { return }

        // Wait for optional delay (JS/CSS to settle)
        DispatchQueue.main.asyncAfter(deadline: .now() + (delay / 1000.0)) { [weak self] in
            self?.captureFullPage()
        }
    }

    private func captureFullPage() {
        guard let webView = webView else { return }

        // -------------------------------------------------------------
        // 1. Get content size via JavaScript
        let script = """
        (function() {
            const body = document.body;
            const html = document.documentElement;
            const width = Math.max(body.scrollWidth, body.offsetWidth, html.clientWidth, html.scrollWidth, html.offsetWidth);
            const height = Math.max(body.scrollHeight, body.offsetHeight, html.clientHeight, html.scrollHeight, html.offsetHeight);
            return JSON.stringify({width: width, height: height});
        })();
        """

        webView.evaluateJavaScript(script) { [weak self] (result, error) in
            guard let self = self,
                  let jsonString = result as? String,
                  let data = jsonString.data(using: .utf8),
                  let size = try? JSONDecoder().decode(ContentSize.self, from: data),
                  size.width > 0, size.height > 0 else {
                self.resultCallback?(FlutterStandardTypedData(bytes: Data()))
                self.dispose()
                return
            }

            self.takeSnapshot(width: CGFloat(size.width), height: CGFloat(size.height))
        }
    }

    private func takeSnapshot(width: CGFloat, height: CGFloat) {
        guard let webView = webView else { return }

        // -------------------------------------------------------------
        // 2. Resize web view to content size
        webView.frame = CGRect(x: 0, y: 0, width: width, height: height)

        // -------------------------------------------------------------
        // 3. Take snapshot
        let config = WKSnapshotConfiguration()
        config.rect = CGRect(x: 0, y: 0, width: width, height: height)

        webView.takeSnapshot(with: config) { [weak self] (image, error) in
            guard let self = self, let image = image else {
                self.resultCallback?(FlutterStandardTypedData(bytes: Data()))
                self.dispose()
                return
            }

            // -------------------------------------------------------------
            // 4. Convert NSImage → PNG Data
            guard let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil),
                  let bitmap = NSBitmapImageRep(cgImage: cgImage),
                  let pngData = bitmap.representation(using: .png, properties: [:]) else {
                self.resultCallback?(FlutterStandardTypedData(bytes: Data()))
                self.dispose()
                return
            }

            let typedData = FlutterStandardTypedData(bytes: pngData)
            self.resultCallback?(typedData)
            self.dispose()
        }
    }
}

// MARK: - Helper structs
private struct ContentSize: Decodable {
    let width: Int
    let height: Int
}

// MARK: - Convenience: store user info in WKWebView
extension WKWebView {
    var customUserInfo: [String: Any]? {
        get { return objc_getAssociatedObject(self, &AssociatedKeys.userInfo) as? [String: Any] }
        set { objc_setAssociatedObject(self, &AssociatedKeys.userInfo, newValue, .OBJC_ASSOCIATION_RETAIN_NONATOMIC) }
    }
}

private struct AssociatedKeys {
    static var userInfo = "customUserInfo"
}