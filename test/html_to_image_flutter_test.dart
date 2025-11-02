import 'package:flutter_test/flutter_test.dart';
import 'package:html_to_image_flutter/html_to_image_flutter.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_platform_interface.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockHtmlToImageFlutterPlatform
    with MockPlatformInterfaceMixin
    implements HtmlToImageFlutterPlatform {

  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final HtmlToImageFlutterPlatform initialPlatform = HtmlToImageFlutterPlatform.instance;

  test('$MethodChannelHtmlToImageFlutter is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelHtmlToImageFlutter>());
  });

  test('getPlatformVersion', () async {
    HtmlToImageFlutter htmlToImageFlutterPlugin = HtmlToImageFlutter();
    MockHtmlToImageFlutterPlatform fakePlatform = MockHtmlToImageFlutterPlatform();
    HtmlToImageFlutterPlatform.instance = fakePlatform;

    expect(await htmlToImageFlutterPlugin.getPlatformVersion(), '42');
  });
}
