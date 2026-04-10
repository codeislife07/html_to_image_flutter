## 1.1.0

* Improved plugin compatibility and rendering reliability
 - Added config-style API support similar to `html_to_image`, including layout, capture, margins, and webview configuration objects
 - Kept backward compatibility with existing `width`-based calls
 - Improved Android rendering to avoid white/blank captures by keeping the WebView renderable while positioned off-screen
 - Improved iOS rendering readiness checks so snapshots wait for layout, images, and fonts before capture
 - Preserved offline conversion support using local HTML content and asset-based rendering
 - Kept Windows support in the local plugin implementation
 - Improved receipt-print use cases with white background and dark text defaults for more reliable output

## 1.0.0

* Initial release
 - Convert HTML content to image
 - Convert HTML asset to image
