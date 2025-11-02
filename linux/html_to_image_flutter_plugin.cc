// linux/html_to_image_flutter_plugin.cpp
#include "html_to_image_flutter_plugin.h"
#include <webkit2/webkit2.h>
#include <gdk/gdk.h>
#include <flutter_linux/flutter_linux.h>

static void on_load_changed(WebKitWebView *web_view,
                            WebKitLoadEvent load_event,
                            gpointer user_data) {
  auto *data = static_cast<std::tuple<
      std::string, double,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>>*>(user_data);

  if (load_event != WEBKIT_LOAD_FINISHED) return;

  auto &[html, delay_ms, result] = *data;
  g_usleep(static_cast<gulong>(delay_ms * 1000));

  // Get content size
  JSCContext *js = webkit_web_view_get_javascript_global_context(web_view);
  JSCValue *w = webkit_javascript_result_get_global_object(js);
  // Execute script
  const char *script = R"(
    (function(){
      const w = Math.max(document.documentElement.scrollWidth, document.body.scrollWidth);
      const h = Math.max(document.documentElement.scrollHeight, document.body.scrollHeight);
      return JSON.stringify({w:w,h:h});
    })();
  )";
  JSCValue *res = webkit_javascript_result_get_value(
      webkit_web_view_run_javascript(web_view, script, nullptr, nullptr, nullptr));

  // Parse JSON
  gchar *json = jsc_value_to_json(res, 0);
  int width = 0, height = 0;
  // simple parse...
  sscanf(json, "{\"w\":%d,\"h\":%d}", &width, &height);
  g_free(json);

  if (width <= 0 || height <= 0) {
    result->Success(flutter::EncodableValue(std::vector<uint8_t>{}));
    return;
  }

  // Resize the webview
  gtk_widget_set_size_request(GTK_WIDGET(web_view), width, height);

  // Snapshot
  GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(web_view));
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t *cr = cairo_create(surface);
  gtk_widget_draw(GTK_WIDGET(web_view), cr);
  cairo_destroy(cr);

  // Convert to PNG
  guchar *png_data = nullptr;
  gsize png_size = 0;
  cairo_surface_write_to_png_stream(surface,
      <a href="cairo_status_t, const unsigned char *data, unsigned int length, void *closure" target="_blank" rel="noopener noreferrer nofollow"></a> {
        auto *vec = static_cast<std::vector<uint8_t>*>(closure);
        vec->insert(vec->end(), data, data + length);
        return CAIRO_STATUS_SUCCESS;
      }, &png_size);

  std::vector<uint8_t> png_vec(png_data, png_data + png_size);
  result->Success(flutter::EncodableValue(png_vec));

  // Cleanup
  cairo_surface_destroy(surface);
  g_object_unref(web_view);
  delete data;
}

void HtmlToImageFlutterPlugin::ConvertToImage(
    const flutter::EncodableMap &args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

  auto content_it = args.find(flutter::EncodableValue("content"));
  auto delay_it   = args.find(flutter::EncodableValue("delay"));
  std::string html = std::get<std::string>(content_it->second);
  double delay_ms  = delay_it != args.end() ? std::get<double>(delay_it->second) : 200.0;

  std::string wrapped = R"(<!DOCTYPE html><html><head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <style>body{margin:0;padding:0;}</style>
    </head><body>)" + html + "</body></html>";

  // Off-screen WebView
  GtkWidget *web_view = webkit_web_view_new();
  gtk_widget_realize(web_view);
  gtk_widget_set_size_request(web_view, 800, 600); // temporary

  auto *user_data = new std::tuple<std::string, double,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>>{
      std::move(html), delay_ms, std::move(result)};

  g_signal_connect(web_view, "load-changed",
                   G_CALLBACK(on_load_changed), user_data);

  webkit_web_view_load_html(WEBKIT_WEB_VIEW(web_view), wrapped.c_str(), nullptr);
}