package codingislife.htmltoimageflutter

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Size
import android.view.View
import android.view.ViewGroup
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import org.json.JSONArray
import java.io.ByteArrayOutputStream
import kotlin.math.absoluteValue

class HtmlToImageFlutterPlugin : FlutterPlugin, MethodChannel.MethodCallHandler, ActivityAware {

    private lateinit var channel: MethodChannel
    private lateinit var activity: Activity
    private lateinit var context: Context

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "html_to_image_flutter")
        channel.setMethodCallHandler(this)
        context = flutterPluginBinding.applicationContext
        // Must be called BEFORE any WebView instance is created.
        // Enables software-based whole-document drawing so draw(canvas) captures
        // the full content instead of returning a blank/white image.
        WebView.enableSlowWholeDocumentDraw()
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val method = call.method
        val arguments = call.arguments as Map<*, *>
        val rawContent = arguments["content"] as String
        val delay = arguments["delay"] as Int? ?: 500
        val width = arguments["width"] as Int?

        if (method == "convertToImage") {
            // Use Activity context (not application context) — required for
            // WebView to access window token and render properly.
            val webView = WebView(activity).apply {
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                settings.databaseEnabled = true
                settings.useWideViewPort = true
                settings.loadWithOverviewMode = true
                settings.allowFileAccess = true
                settings.allowContentAccess = true
                isHorizontalScrollBarEnabled = false
                isVerticalScrollBarEnabled = false
                setInitialScale(100)
                // Force software rendering so draw(canvas) captures full content.
                // Hardware-accelerated layers are not accessible via draw() on
                // views that are not attached to a display surface.
                setLayerType(View.LAYER_TYPE_SOFTWARE, null)
                setBackgroundColor(android.graphics.Color.WHITE)
                alpha = 0.01f
                translationX = -10000f
                translationY = -10000f
            }

            val targetWidth = width ?: getDisplaySize().width

            val fullHtml = """
                <!DOCTYPE html><html>
                <head>
                    <meta charset="utf-8">
                    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
                    <style>
                        :root { color-scheme: light only; }
                        * {
                            box-sizing: border-box;
                            -webkit-print-color-adjust: exact !important;
                            print-color-adjust: exact !important;
                        }
                        html, body {
                            margin: 0; padding: 0;
                            background: #FFFFFF !important;
                            color: #111111 !important;
                        }
                        body {
                            width: 100%; max-width: ${targetWidth}px;
                            overflow-wrap: break-word;
                            word-wrap: break-word;
                            overflow: hidden;
                            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif;
                            text-rendering: optimizeLegibility;
                        }
                        img, svg, canvas, video { max-width: 100%; height: auto; display: block; }
                        table { border-collapse: collapse; width: 100%; }
                        a { color: #111111 !important; }
                    </style>
                </head>
                <body>$rawContent</body>
                </html>
            """.trimIndent()

            webView.webViewClient = object : WebViewClient() {
                override fun onPageFinished(view: WebView, url: String) {
                    // onPageFinished runs on the main thread; start the
                    // content-ready check directly from here.
                    checkContentRendered(
                        webView = view,
                        waitMs = delay.toLong(),
                        totalWaitMs = delay.toLong(),
                        result = result,
                        targetWidth = targetWidth,
                    )
                }

                override fun onReceivedError(
                    view: WebView?,
                    request: WebResourceRequest?,
                    error: WebResourceError?
                ) {
                    removeFromWindow(webView)
                    result.error("WEBVIEW_ERROR", "Failed to load: ${error?.description}", null)
                }
            }

            // ── Attach WebView to the Activity window BEFORE loading HTML ──────
            // draw(canvas) only works correctly when the WebView is part of a
            // live window hierarchy. Without this, draw() returns a white bitmap
            // regardless of the content.
            val decorView = activity.window.decorView as ViewGroup
            decorView.addView(
                webView,
                ViewGroup.LayoutParams(targetWidth, ViewGroup.LayoutParams.WRAP_CONTENT)
            )
            webView.measure(
                View.MeasureSpec.makeMeasureSpec(targetWidth, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED)
            )
            webView.layout(0, 0, targetWidth, webView.measuredHeight)
            webView.loadDataWithBaseURL(null, fullHtml, "text/html", "UTF-8", null)

        } else {
            result.notImplemented()
        }
    }

    // Remove WebView from the window after capture (or on error).
    private fun removeFromWindow(webView: WebView) {
        Handler(Looper.getMainLooper()).post {
            (webView.parent as? ViewGroup)?.removeView(webView)
        }
    }

    private fun checkContentRendered(
        webView: WebView,
        waitMs: Long,
        totalWaitMs: Long,
        result: MethodChannel.Result,
        targetWidth: Int
    ) {
        Handler(Looper.getMainLooper()).postDelayed({
            webView.evaluateJavascript(
                """
                (function() {
                    var body = document.body;
                    var doc = document.documentElement;
                    var images = document.getElementsByTagName('img');
                    var imagesLoaded = true;
                    for (var i = 0; i < images.length; i++) {
                        if (!images[i].complete) { imagesLoaded = false; break; }
                    }
                    var fontsLoaded = true;
                    if (document.fonts && document.fonts.status) {
                        fontsLoaded = document.fonts.status === 'loaded';
                    }
                    var rects = body ? body.getClientRects() : [];
                    return {
                        width: Math.max(body ? body.scrollWidth : 0, doc ? doc.scrollWidth : 0),
                        height: Math.max(body ? body.scrollHeight : 0, doc ? doc.scrollHeight : 0),
                        hasLayout: rects && rects.length > 0,
                        textLength: body && body.innerText ? body.innerText.trim().length : 0,
                        imagesLoaded: imagesLoaded,
                        fontsLoaded: fontsLoaded
                    };
                })();
                """
            ) { value ->
                try {
                    val json = JSONArray("[$value]").getJSONObject(0)
                    val contentWidth  = json.getDouble("width").absoluteValue.toInt()
                    val contentHeight = json.getDouble("height").absoluteValue.toInt()
                    val hasLayout     = json.optBoolean("hasLayout", false)
                    val textLength    = json.optInt("textLength", 0)
                    val imagesLoaded  = json.optBoolean("imagesLoaded", true)
                    val fontsLoaded   = json.optBoolean("fontsLoaded", true)

                    val contentReady = contentWidth > 0 &&
                        contentHeight > 0 &&
                        hasLayout &&
                        (textLength > 0 || imagesLoaded)

                    if ((!contentReady || !imagesLoaded || !fontsLoaded) && totalWaitMs < 5000) {
                        checkContentRendered(
                            webView = webView,
                            waitMs = 250L,
                            totalWaitMs = totalWaitMs + 250L,
                            result = result,
                            targetWidth = targetWidth,
                        )
                        return@evaluateJavascript
                    }

                    if (contentWidth <= 0 || contentHeight <= 0) {
                        removeFromWindow(webView)
                        result.error("INVALID_SIZE", "Content size invalid: ${contentWidth}x${contentHeight}", null)
                        return@evaluateJavascript
                    }

                    val w = if (targetWidth > 0) targetWidth else contentWidth

                    // Resize WebView to exact content dimensions before drawing
                    webView.measure(
                        View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(contentHeight, View.MeasureSpec.EXACTLY)
                    )
                    webView.layout(0, 0, w, contentHeight)
                    webView.invalidate()

                    val bitmap = webView.toBitmap(w.toDouble(), contentHeight.toDouble())
                    removeFromWindow(webView)

                    if (bitmap != null) {
                        result.success(bitmap.toByteArray())
                    } else {
                        result.error("BITMAP_NULL", "Failed to generate image", null)
                    }
                } catch (e: Exception) {
                    removeFromWindow(webView)
                    result.error("EVALUATION_ERROR", "JS evaluation failed: ${e.message}", null)
                }
            }
        }, waitMs)
    }

    @Suppress("DEPRECATION")
    private fun getDisplaySize(): Size {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val bounds = activity.windowManager.currentWindowMetrics.bounds
            Size(bounds.width(), bounds.height())
        } else {
            val display = activity.windowManager.defaultDisplay
            Size(display.width, display.height)
        }
    }

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
    }

    override fun onDetachedFromActivityForConfigChanges() {}
    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        onAttachedToActivity(binding)
    }

    override fun onDetachedFromActivity() {}

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
    }
}

fun WebView.toBitmap(offsetWidth: Double, offsetHeight: Double): Bitmap? {
    if (offsetWidth <= 0 || offsetHeight <= 0) return null
    val w = offsetWidth.absoluteValue.toInt()
    val h = offsetHeight.absoluteValue.toInt()
    measure(
        View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY)
    )
    layout(0, 0, w, h)
    val bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
    draw(Canvas(bitmap))
    return bitmap
}

fun Bitmap.toByteArray(): ByteArray =
    ByteArrayOutputStream().use {
        compress(Bitmap.CompressFormat.PNG, 100, it)
        it.toByteArray()
    }
