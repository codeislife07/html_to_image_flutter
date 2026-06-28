import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:html_to_image_flutter/html_to_image_flutter.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  Uint8List? _imageBytes;
  String? _error;

  @override
  void initState() {
    super.initState();
    _convertOfflineHtml();
  }

  Future<void> _convertOfflineHtml() async {
    try {
      final bytes = await HtmlToImage.convertToImage(
        content: '''
          <div style="width:320px;padding:20px;background:white;color:#111;font-family:Arial">
            <h2 style="margin:0 0 8px">Offline receipt</h2>
            <p style="margin:0">Rendered from local HTML content.</p>
          </div>
        ''',
        width: 320,
      );

      if (!mounted) return;
      setState(() {
        _imageBytes = bytes;
      });
    } catch (error) {
      if (!mounted) return;
      setState(() {
        _error = error.toString();
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('Plugin example app')),
        body: Center(
          child: _error != null
              ? Text(_error!)
              : _imageBytes == null
              ? const CircularProgressIndicator()
              : Image.memory(_imageBytes!),
        ),
      ),
    );
  }
}
