import 'package:flutter_test/flutter_test.dart';
import 'package:html_to_image_flutter/html_to_image_flutter.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('converts offline HTML content to image bytes', (_) async {
    final bytes = await HtmlToImage.convertToImage(
      content: '<div style="padding:12px;background:white">Offline</div>',
      width: 240,
    );

    expect(bytes, isNotEmpty);
  });
}
