class LayoutStrategy {
  final int? width;
  final int? height;

  const LayoutStrategy._(this.width, this.height);

  const LayoutStrategy.deviceDefault()
      : width = null,
        height = null;

  const LayoutStrategy.withDimensions({
    this.width,
    this.height,
  });

  const LayoutStrategy.a0() : this._(2384, 3370);
  const LayoutStrategy.a1() : this._(1684, 2384);
  const LayoutStrategy.a2() : this._(1191, 1684);
  const LayoutStrategy.a3() : this._(842, 1191);
  const LayoutStrategy.a4() : this._(595, 842);
  const LayoutStrategy.a5() : this._(420, 595);
  const LayoutStrategy.a6() : this._(298, 420);
  const LayoutStrategy.a7() : this._(210, 298);
  const LayoutStrategy.a8() : this._(147, 210);
  const LayoutStrategy.a9() : this._(105, 147);
  const LayoutStrategy.a10() : this._(74, 105);
  const LayoutStrategy.t80() : this._(227, null);
  const LayoutStrategy.t76() : this._(215, null);
  const LayoutStrategy.t57() : this._(162, null);

  Map<String, dynamic> toMap() {
    return {
      'width': width,
      'height': height,
    };
  }
}
