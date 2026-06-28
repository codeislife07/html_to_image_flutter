import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:html_to_image_flutter/html_to_image_flutter_method_channel.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  final platform = MethodChannelHtmlToImage();
  const MethodChannel channel = MethodChannel('html_to_image_flutter');

  setUp(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
      channel,
      (MethodCall methodCall) async {
        expect(methodCall.method, 'convertToImage');
        expect(methodCall.arguments['content'], '<p>Offline HTML</p>');
        return Uint8List.fromList([4, 5, 6]);
      },
    );
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('convertToImage sends content over method channel', () async {
    final bytes = await platform.convertToImage(content: '<p>Offline HTML</p>');

    expect(bytes, [4, 5, 6]);
  });
}
