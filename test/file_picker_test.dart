import 'package:flutter/services.dart';
import 'package:test/test.dart';
import 'package:file_picker/file_picker.dart';

void main() {
  const MethodChannel channel = MethodChannel('file_picker');

  setUp(() {
    channel.setMockMethodCallHandler((MethodCall methodCall) async {
      return '42';
    });
  });

  tearDown(() {
    channel.setMockMethodCallHandler(null);
  });

  test('getPlatformVersion', () async {});
}
