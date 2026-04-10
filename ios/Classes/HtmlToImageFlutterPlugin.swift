import Flutter
import UIKit
import WebKit

public class HtmlToImageFlutterPlugin: NSObject, FlutterPlugin {

    private var webView: WKWebView?
    private var pendingResult: FlutterResult?
    private var delayMs: Double = 200.0
    private var widthHint: CGFloat = 0
    private var totalWaitMs: Double = 0

    public static func register(with registrar: FlutterPluginRegistrar) {
        let channel = FlutterMethodChannel(
            name: "html_to_image_flutter",
            binaryMessenger: registrar.messenger())
        let instance = HtmlToImageFlutterPlugin()
        registrar.addMethodCallDelegate(instance, channel: channel)
    }

    public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
        guard let arguments = call.arguments as? [String: Any],
              let content = arguments["content"] as? String else {
            result(FlutterError(code: "INVALID_ARGUMENTS",
                                message: "Missing 'content'", details: nil))
            return
        }

        delayMs   = (arguments["delay"] as? Double) ?? 200.0
        widthHint = CGFloat((arguments["width"] as? Int) ?? 0)
        totalWaitMs = delayMs

        switch call.method {
        case "convertToImage":
            pendingResult = result

            let initWidth = widthHint > 0 ? widthHint : UIScreen.main.bounds.width

            let htmlWithViewport = """
            <!DOCTYPE html><html>
            <head>
              <meta charset="utf-8">
              <meta name="viewport" content="width=device-width, initial-scale=1.0">
              <style>
                :root{color-scheme:light only;}
                *{
                  box-sizing:border-box;
                  -webkit-print-color-adjust:exact !important;
                  print-color-adjust:exact !important;
                }
                html,body{
                  margin:0;
                  padding:0;
                  background:#FFFFFF !important;
                  color:#111111 !important;
                }
                body{
                  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
                  text-rendering:optimizeLegibility;
                  overflow-wrap:break-word;
                  word-wrap:break-word;
                }
                img,svg,canvas,video{
                  max-width:100%;
                  height:auto;
                  display:block;
                }
                table{
                  border-collapse:collapse;
                  width:100%;
                }
                a{color:#111111 !important;}
              </style>
            </head>
            <body>\(content)</body>
            </html>
            """

            let config = WKWebViewConfiguration()
            // Place WebView off-screen but in the window hierarchy.
            // takeSnapshot requires the WebView to be attached to a window;
            // a view that is hidden (isHidden=true) or has no window returns blank.
            let offScreenFrame = CGRect(x: 0, y: -10000, width: initWidth, height: 100)
            let wv = WKWebView(frame: offScreenFrame, configuration: config)
            wv.navigationDelegate = self
            wv.scrollView.showsVerticalScrollIndicator   = false
            wv.scrollView.showsHorizontalScrollIndicator = false
            wv.isOpaque = true
            wv.backgroundColor = .white
            wv.scrollView.backgroundColor = .white
            // isHidden MUST be false for takeSnapshot to capture real content.
            wv.isHidden = false

            // Add to the key window so the WebView has a window context.
            // It is positioned far off-screen so the user never sees it.
            let window = UIApplication.shared.windows.first(where: { $0.isKeyWindow })
                      ?? UIApplication.shared.windows.first
            window?.addSubview(wv)

            self.webView = wv
            wv.loadHTMLString(htmlWithViewport, baseURL: nil)

        default:
            result(FlutterMethodNotImplemented)
        }
    }

    // Cleanup: remove from window, release resources, send result.
    private func finish(pngData: Data?) {
        webView?.removeFromSuperview()
        webView?.navigationDelegate = nil
        webView = nil

        let bytes = pngData ?? Data()
        pendingResult?(FlutterStandardTypedData(bytes: bytes))
        pendingResult = nil
    }
}

// MARK: - WKNavigationDelegate
// Using WKNavigationDelegate instead of KVO on isLoading.
// KVO fires when loading *starts* AND when it ends; we only want didFinish.
extension HtmlToImageFlutterPlugin: WKNavigationDelegate {

    public func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        let delay = delayMs / 1000.0
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            self?.captureContentWhenReady(webView)
        }
    }

    public func webView(_ webView: WKWebView, didFail navigation: WKNavigation!,
                        withError error: Error) {
        finish(pngData: nil)
    }

    public func webView(_ webView: WKWebView,
                        didFailProvisionalNavigation navigation: WKNavigation!,
                        withError error: Error) {
        finish(pngData: nil)
    }

    private func captureContentWhenReady(_ webView: WKWebView) {
        let readinessScript = """
        (function(){
            var b=document.body, d=document.documentElement;
            var images=document.getElementsByTagName('img');
            var imagesLoaded=true;
            for (var i=0;i<images.length;i++){
                if(!images[i].complete){imagesLoaded=false;break;}
            }
            var fontsLoaded=true;
            if(document.fonts && document.fonts.status){
                fontsLoaded=document.fonts.status==='loaded';
            }
            var rects=b ? b.getClientRects() : [];
            var textLength=b && b.innerText ? b.innerText.trim().length : 0;
            var w=Math.max(b?b.scrollWidth:0, d?d.scrollWidth:0, 1);
            var h=Math.max(b?b.scrollHeight:0, d?d.scrollHeight:0, 1);
            return JSON.stringify({
                w:w,
                h:h,
                hasLayout: !!(rects && rects.length),
                textLength:textLength,
                imagesLoaded:imagesLoaded,
                fontsLoaded:fontsLoaded
            });
        })()
        """

        webView.evaluateJavaScript(readinessScript) { [weak self] res, _ in
            guard let self = self else { return }

            var width: CGFloat = self.widthHint > 0 ? self.widthHint : UIScreen.main.bounds.width
            var height: CGFloat = 1
            var hasLayout = false
            var textLength = 0
            var imagesLoaded = true
            var fontsLoaded = true

            if let str = res as? String,
               let data = str.data(using: .utf8),
               let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                if self.widthHint <= 0, let w = json["w"] as? CGFloat { width = w }
                if let h = json["h"] as? CGFloat { height = h }
                hasLayout = json["hasLayout"] as? Bool ?? false
                textLength = json["textLength"] as? Int ?? 0
                imagesLoaded = json["imagesLoaded"] as? Bool ?? true
                fontsLoaded = json["fontsLoaded"] as? Bool ?? true
            }

            let contentReady = width > 0 && height > 0 && hasLayout && (textLength > 0 || imagesLoaded)
            if (!contentReady || !imagesLoaded || !fontsLoaded), self.totalWaitMs < 5000 {
                self.totalWaitMs += 250
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
                    self?.captureContentWhenReady(webView)
                }
                return
            }

            self.captureContent(webView, width: width, height: max(height, 1))
        }
    }

    // Step 1: measure actual content size via JS
    private func captureContent(_ webView: WKWebView, width: CGFloat, height: CGFloat) {
        let script = """
        (function(){
            var b=document.body, d=document.documentElement;
            var w=Math.max(b?b.scrollWidth:0, d?d.scrollWidth:0, 1);
            var h=Math.max(b?b.scrollHeight:0, d?d.scrollHeight:0, 1);
            return JSON.stringify({w:w, h:h});
        })()
        """
        webView.evaluateJavaScript(script) { [weak self] res, _ in
            guard let self = self else { return }

            var w: CGFloat = width
            var h: CGFloat = height

            if let str = res as? String,
               let data = str.data(using: .utf8),
               let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                if self.widthHint <= 0, let pw = json["w"] as? CGFloat { w = pw }
                if let ph = json["h"] as? CGFloat { h = ph }
            }

            self.takeSnapshot(webView, width: w, height: h)
        }
    }

    // Step 2: resize to content dimensions, then snapshot
    private func takeSnapshot(_ webView: WKWebView, width: CGFloat, height: CGFloat) {
        // Move off-screen with the correct final size so the layout is stable
        webView.frame = CGRect(x: 0, y: -(height + 100), width: width, height: height)

        let snapConfig = WKSnapshotConfiguration()
        snapConfig.rect = CGRect(origin: .zero, size: CGSize(width: width, height: height))

        webView.takeSnapshot(with: snapConfig) { [weak self] image, _ in
            guard let self = self, let image = image else {
                self?.finish(pngData: nil)
                return
            }
            self.finish(pngData: image.pngData())
        }
    }
}
