import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:html_to_image_flutter/html_to_image_flutter.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  const channel = MethodChannel('html_to_image_flutter');

  setUp(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (methodCall) async {
      expect(methodCall.method, 'convertToImage');
      return Uint8List.fromList([7, 8, 9]);
    });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });

  test('HtmlToImage converts HTML content through the default platform',
      () async {
    final bytes = await HtmlToImage.convertToImage(
      content: '<strong>Offline ready</strong>',
    );

    expect(bytes, [7, 8, 9]);
  });
}
