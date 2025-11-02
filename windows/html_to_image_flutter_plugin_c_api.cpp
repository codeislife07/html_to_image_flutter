#include "include/html_to_image_flutter/html_to_image_flutter_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "html_to_image_flutter_plugin.h"

void HtmlToImageFlutterPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  html_to_image_flutter::HtmlToImageFlutterPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
