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
            }

            val targetWidth = width ?: getDisplaySize().width

            val fullHtml = """
                <!DOCTYPE html><html>
                <head>
                    <meta charset="utf-8">
                    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
                    <style>
                        * { box-sizing: border-box; }
                        body {
                            margin: 0; padding: 0;
                            width: 100%; max-width: ${targetWidth}px;
                            overflow-wrap: break-word;
                            word-wrap: break-word;
                            overflow: hidden;
                        }
                        img { max-width: 100%; height: auto; display: block; }
                    </style>
                </head>
                <body>$rawContent</body>
                </html>
            """.trimIndent()

            webView.webViewClient = object : WebViewClient() {
                override fun onPageFinished(view: WebView, url: String) {
                    // onPageFinished runs on the main thread; start the
                    // content-ready check directly from here.
                    checkContentRendered(view, delay.toLong(), result, targetWidth)
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
            webView.visibility = View.INVISIBLE
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
        delay: Long,
        result: MethodChannel.Result,
        targetWidth: Int
    ) {
        Handler(Looper.getMainLooper()).postDelayed({
            webView.evaluateJavascript(
                """
                (function() {
                    var images = document.getElementsByTagName('img');
                    var loaded = true;
                    for (var i = 0; i < images.length; i++) {
                        if (!images[i].complete) { loaded = false; break; }
                    }
                    return {
                        width: document.body.scrollWidth,
                        height: document.body.scrollHeight,
                        fullyLoaded: loaded
                    };
                })();
                """
            ) { value ->
                try {
                    val json = JSONArray("[$value]").getJSONObject(0)
                    val contentWidth  = json.getDouble("width").absoluteValue.toInt()
                    val contentHeight = json.getDouble("height").absoluteValue.toInt()
                    val fullyLoaded   = json.getBoolean("fullyLoaded")

                    if (!fullyLoaded && delay < 5000) {
                        // Images still loading — retry up to 5 seconds total
                        checkContentRendered(webView, delay + 500, result, targetWidth)
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
        }, delay)
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
