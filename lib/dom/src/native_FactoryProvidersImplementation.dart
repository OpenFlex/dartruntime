// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// These factory provider implementation functions are for interfaces that do
// not have factory provider implementation functions generated automatically
// from a Constructor or NamedConstructor extended attribute.

class FactoryProviderImplementation {
  static AudioContext createAudioContext() native "AudioContext_constructor_Callback";

  static Float32Array F32(_arg0, [_arg1, _arg2]) native "Float32Array_constructor_Callback";
  static Float64Array F64(_arg0, [_arg1, _arg2]) native "Float64Array_constructor_Callback";
  static Int8Array I8(_arg0, [_arg1, _arg2]) native "Int8Array_constructor_Callback";
  static Int16Array I16(_arg0, [_arg1, _arg2]) native "Int16Array_constructor_Callback";
  static Int32Array I32(_arg0, [_arg1, _arg2]) native "Int32Array_constructor_Callback";
  static Uint8Array U8(_arg0, [_arg1, _arg2]) native "Uint8Array_constructor_Callback";
  static Uint8Array U8C(_arg0, [_arg1, _arg2]) native "Uint8ClampedArray_constructor_Callback";
  static Uint16Array U16(_arg0, [_arg1, _arg2]) native "Uint16Array_constructor_Callback";
  static Uint32Array U32(_arg0, [_arg1, _arg2]) native "Uint32Array_constructor_Callback";
  
  static WebKitPoint createWebKitPoint(num x, num y) native "WebKitPoint_constructor_Callback";
  static WebSocket createWebSocket(String url) native "WebSocket_constructor_Callback";

  static IDBKeyRange IDBKeyRange_only(value) => IDBKeyRangeImplementation.only(value);
  static IDBKeyRange IDBKeyRange_lowerBound(bound, open) => IDBKeyRangeImplementation.lowerBound(bound, open);
  static IDBKeyRange IDBKeyRange_upperBound(bound, open) => IDBKeyRangeImplementation.upperBound(bound, open);
  static IDBKeyRange IDBKeyRange_bound(lower, upper, lowerOpen, upperOpen) => IDBKeyRangeImplementation.bound(lower, upper,lowerOpen, upperOpen);
}
