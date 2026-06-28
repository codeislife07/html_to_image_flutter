import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:html_to_image_flutter/html_to_image_flutter.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_method_channel.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_platform_interface.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockHtmlToImagePlatform
    with MockPlatformInterfaceMixin
    implements HtmlToImagePlatform {
  String? lastContent;

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
  }) {
    lastContent = content;
    return Future.value(Uint8List.fromList([1, 2, 3]));
  }
}

void main() {
  final HtmlToImagePlatform initialPlatform = HtmlToImagePlatform.instance;

  test('$MethodChannelHtmlToImage is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelHtmlToImage>());
  });

  test('convertToImage delegates HTML content to the platform', () async {
    final fakePlatform = MockHtmlToImagePlatform();
    HtmlToImagePlatform.instance = fakePlatform;

    final bytes = await HtmlToImage.convertToImage(
      content: '<p>Offline HTML</p>',
    );

    expect(bytes, [1, 2, 3]);
    expect(fakePlatform.lastContent, '<p>Offline HTML</p>');
  });
}
