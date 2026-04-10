import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:html_to_image_flutter/config/config.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_platform_interface.dart';

/// An implementation of [HtmlToImagePlatform] that uses method channels.
class MethodChannelHtmlToImage extends HtmlToImagePlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('html_to_image_flutter');

  @override
  Future<Uint8List> convertToImage({
    required String content,
    Duration delay = const Duration(milliseconds: 200),
    int? width,
    ImageMargins margins = const ImageMargins(),
    bool useDeviceScaleFactor = false,
    LayoutStrategy layoutStrategy = const LayoutStrategy.deviceDefault(),
    CaptureStrategy captureStrategy = const CaptureStrategy.followLayout(),
    WebViewConfiguration webViewConfiguration = const WebViewConfiguration(),
  }) async {
    final Map<String, dynamic> arguments = {
      'content': content,
      'delay': delay.inMilliseconds,
      'width': width,
      'margins': margins.toMap(),
      'use_device_scale_factor': useDeviceScaleFactor,
      'layout_strategy': layoutStrategy.toMap(),
      'capture_strategy': captureStrategy.toMap(),
      'web_view_configuration': webViewConfiguration.toMap(),
    };
    try {
      final result = await (methodChannel.invokeMethod(
          'convertToImage', arguments)) as Uint8List;
      return result;
    } on Exception catch (e) {
      throw Exception("Error: $e");
    }
  }
}
