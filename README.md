# html_to_image_flutter

Flutter plugin to convert HTML content or HTML asset files to PNG image bytes
using WebView on Android and WKWebView on iOS.

# Requirements
- Android: Minimum SDK Version 21
- iOS: Minimum Deployment Target 11.0

# Usage

## Convert to Image from HTML content
- ```Future<Uint8List> convertToImage(String content, Duration delay,int? width)```
```dart
final imageBytes = await HtmlToImage.convertToImage(
  content: content,
);
final image = Image.memory(imageBytes);
```

## Convert to Image from HTML asset
- ```Future<Uint8List> convertToImageFromAsset(String asset, Duration delay,int? width)```
```dart
final imageBytes = await HtmlToImage.convertToImageFromAsset(
  asset: 'assets/example.html',
);
final image = Image.memory(imageBytes);
```

## Offline support

`html_to_image_flutter` renders HTML passed from Dart directly inside the native
WebView, so plain HTML strings and bundled Flutter assets can be converted while
the device is offline.

For fully offline output, keep all required CSS, fonts, and images available
locally:

```dart
final imageBytes = await HtmlToImage.convertToImage(
  content: '''
    <div style="padding:16px;font-family:Arial;background:white;color:#111">
      <h1>Offline receipt</h1>
      <p>This HTML does not require network access.</p>
    </div>
  ''',
  width: 360,
);
```

Recommended offline patterns:
- Inline critical CSS in the HTML.
- Use `convertToImageFromAsset` for bundled HTML templates.
- Use base64/data URI images or local asset content injected into the HTML.
- Avoid depending on remote image, font, or stylesheet URLs when deterministic
  offline captures are required.

- Default delay is 200 milliseconds
- Default width is device width
