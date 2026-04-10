import 'package:flutter/services.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_platform_interface.dart';
import 'package:html_to_image_flutter/config/config.dart';

export 'package:html_to_image_flutter/config/config.dart';

class HtmlToImage {
  /// Converts the given HTML asset file to an image.
  ///
  /// [asset] Asset path to HTML file
  ///
  /// [delay] The delay before taking the snapshot.
  /// This is useful when the content has animations, images or other dynamic content.
  ///
  /// [width] Required width of the image.
  static Future<Uint8List> convertToImageFromAsset({
    required String asset,
    Duration delay = const Duration(milliseconds: 200),
    int? width,
    ImageMargins margins = const ImageMargins(),
    bool useDeviceScaleFactor = false,
    LayoutStrategy layoutStrategy = const LayoutStrategy.deviceDefault(),
    CaptureStrategy captureStrategy = const CaptureStrategy.followLayout(),
    WebViewConfiguration webViewConfiguration = const WebViewConfiguration(),
  }) async {
    final content = await rootBundle.loadString(asset);
    return HtmlToImagePlatform.instance.convertToImage(
      content: content,
      delay: delay,
      width: width,
      margins: margins,
      useDeviceScaleFactor: useDeviceScaleFactor,
      layoutStrategy: layoutStrategy,
      captureStrategy: captureStrategy,
      webViewConfiguration: webViewConfiguration,
    );
  }

  /// Converts the given HTML content to an image.
  ///
  /// [content] Plain HTML content
  ///
  /// [delay] The delay before taking the snapshot.
  /// This is useful when the content has animations, images or other dynamic content.
  ///
  /// [width] Required width of the image.
  static Future<Uint8List> convertToImage({
    required String content,
    Duration delay = const Duration(milliseconds: 200),
    int? width,
    ImageMargins margins = const ImageMargins(),
    bool useDeviceScaleFactor = false,
    LayoutStrategy layoutStrategy = const LayoutStrategy.deviceDefault(),
    CaptureStrategy captureStrategy = const CaptureStrategy.followLayout(),
    WebViewConfiguration webViewConfiguration = const WebViewConfiguration(),
  }) {
    return HtmlToImagePlatform.instance.convertToImage(
      content: content,
      delay: delay,
      width: width,
      margins: margins,
      useDeviceScaleFactor: useDeviceScaleFactor,
      layoutStrategy: layoutStrategy,
      captureStrategy: captureStrategy,
      webViewConfiguration: webViewConfiguration,
    );
  }

  /// Convert the given HTML content to an image and returns null if any error occurs.
  ///
  /// [content] Plain HTML content
  ///
  /// [delay] The delay before taking the snapshot.
  /// This is useful when the content has animations, images or other dynamic content.
  ///
  /// [width] Required width of the image.
  static Future<Uint8List?> tryConvertToImage({
    required String content,
    Duration delay = const Duration(milliseconds: 200),
    int? width,
    ImageMargins margins = const ImageMargins(),
    bool useDeviceScaleFactor = false,
    LayoutStrategy layoutStrategy = const LayoutStrategy.deviceDefault(),
    CaptureStrategy captureStrategy = const CaptureStrategy.followLayout(),
    WebViewConfiguration webViewConfiguration = const WebViewConfiguration(),
  }) async {
    try {
      return await HtmlToImagePlatform.instance.convertToImage(
        content: content,
        delay: delay,
        width: width,
        margins: margins,
        useDeviceScaleFactor: useDeviceScaleFactor,
        layoutStrategy: layoutStrategy,
        captureStrategy: captureStrategy,
        webViewConfiguration: webViewConfiguration,
      );
    } catch (_) {
      return null;
    }
  }
}
