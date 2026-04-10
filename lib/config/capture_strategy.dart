class CaptureStrategy {
  final int? width;
  final int? height;
  final String? script;

  const CaptureStrategy.followLayout()
      : width = null,
        height = null,
        script = null;

  const CaptureStrategy.withDimensions({
    this.width,
    this.height,
  }) : script = null;

  const CaptureStrategy.unbounded()
      : width = -1,
        height = -1,
        script = null;

  const CaptureStrategy.fitContent()
      : width = null,
        height = null,
        script = fitContentJs;

  const CaptureStrategy.fitWidth()
      : width = null,
        height = null,
        script = fitWidthJs;

  const CaptureStrategy.fitHeight()
      : width = null,
        height = null,
        script = fitHeightJs;

  const CaptureStrategy.fullScroll()
      : width = null,
        height = null,
        script = fullScrollJs;

  const CaptureStrategy.customScript(this.script)
      : width = null,
        height = null;

  static const fitContentJs = """
  (function() {
   let maxRight = 0;
   let maxBottom = 0;

    document.body.querySelectorAll('*').forEach(el => {
    const rect = el.getBoundingClientRect();
    maxRight = Math.max(maxRight, rect.right);
    maxBottom = Math.max(maxBottom, rect.bottom);
    });

    return [maxRight, maxBottom];
    })();
  """;

  static const fitWidthJs = """
  (function() {
   let maxRight = 0;

    document.body.querySelectorAll('*').forEach(el => {
    const rect = el.getBoundingClientRect();
    maxRight = Math.max(maxRight, rect.right);
    });

    return [maxRight, 0];
    })();
  """;

  static const fitHeightJs = """
  (function() {
   let maxBottom = 0;

    document.body.querySelectorAll('*').forEach(el => {
    const rect = el.getBoundingClientRect();
    maxBottom = Math.max(maxBottom, rect.bottom);
    });

    return [0, maxBottom];
    })();
  """;

  static const fullScrollJs = """
  (function() {
      var body = document.body;
      var html = document.documentElement;

      var totalWidth = Math.max(
          body.scrollWidth, html.scrollWidth,
          body.offsetWidth, html.offsetWidth,
          body.clientWidth, html.clientWidth
      );

      var totalHeight = Math.max(
          body.scrollHeight, html.scrollHeight,
          body.offsetHeight, html.offsetHeight,
          body.clientHeight, html.clientHeight
      );

      return [totalWidth, totalHeight];
  })();
""";

  Map<String, dynamic> toMap() {
    return {
      'width': width,
      'height': height,
      'script': script,
    };
  }
}
