// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Global constants.
class _Const {
  // Bytes for "HTTP/1.0".
  static final HTTP10 = const [72, 84, 84, 80, 47, 49, 46, 48];
  // Bytes for "HTTP/1.1".
  static final HTTP11 = const [72, 84, 84, 80, 47, 49, 46, 49];

  static final END_CHUNKED = const [0x30, 13, 10, 13, 10];
}


// Frequently used character codes.
class _CharCode {
  static final int HT = 9;
  static final int LF = 10;
  static final int CR = 13;
  static final int SP = 32;
  static final int COLON = 58;
}


// States of the HTTP parser state machine.
class _State {
  static final int START = 0;
  static final int METHOD_OR_HTTP_VERSION = 1;
  static final int REQUEST_LINE_METHOD = 2;
  static final int REQUEST_LINE_URI = 3;
  static final int REQUEST_LINE_HTTP_VERSION = 4;
  static final int REQUEST_LINE_ENDING = 5;
  static final int RESPONSE_LINE_STATUS_CODE = 6;
  static final int RESPONSE_LINE_REASON_PHRASE = 7;
  static final int RESPONSE_LINE_ENDING = 8;
  static final int HEADER_START = 9;
  static final int HEADER_FIELD = 10;
  static final int HEADER_VALUE_START = 11;
  static final int HEADER_VALUE = 12;
  static final int HEADER_VALUE_FOLDING_OR_ENDING = 13;
  static final int HEADER_VALUE_FOLD_OR_END = 14;
  static final int HEADER_ENDING = 15;
  static final int CHUNK_SIZE_STARTING_CR = 16;
  static final int CHUNK_SIZE_STARTING_LF = 17;
  static final int CHUNK_SIZE = 18;
  static final int CHUNK_SIZE_ENDING = 19;
  static final int CHUNKED_BODY_DONE_CR = 20;
  static final int CHUNKED_BODY_DONE_LF = 21;
  static final int BODY = 22;
}


/**
 * HTTP parser which parses the HTTP stream as data is supplied
 * through the writeList method. As the data is parsed the events
 *   RequestStart
 *   UriReceived
 *   HeaderReceived
 *   HeadersComplete
 *   DataReceived
 *   DataEnd
 * are generated.
 * Currently only HTTP requests with Content-Length header are supported.
 */
class _HttpParser {
  _HttpParser()
      : _state = _State.START,
        _failure = false,
        _headerField = new StringBuffer(),
        _headerValue = new StringBuffer(),
        _method_or_status_code = new StringBuffer(),
        _uri_or_reason_phrase = new StringBuffer();

  // From RFC 2616.
  // generic-message = start-line
  //                   *(message-header CRLF)
  //                   CRLF
  //                   [ message-body ]
  // start-line      = Request-Line | Status-Line
  // Request-Line    = Method SP Request-URI SP HTTP-Version CRLF
  // Status-Line     = HTTP-Version SP Status-Code SP Reason-Phrase CRLF
  // message-header  = field-name ":" [ field-value ]
  int writeList(List<int> buffer, int offset, int count) {
    int index = offset;
    int lastIndex = offset + count;
    while ((index < lastIndex) && !_failure) {
      int byte = buffer[index];
      switch (_state) {
        case _State.START:
          _contentLength = 0;
          _keepAlive = false;
          _chunked = false;

          if (byte == _Const.HTTP11[0]) {
            // Start parsing HTTP method.
            _httpVersionIndex = 1;
            _state = _State.METHOD_OR_HTTP_VERSION;
          } else {
            // Start parsing method.
            _method_or_status_code.addCharCode(byte);
            _state = _State.REQUEST_LINE_METHOD;
          }
          break;

        case _State.METHOD_OR_HTTP_VERSION:
          if (_httpVersionIndex < _Const.HTTP11.length &&
              byte == _Const.HTTP11[_httpVersionIndex]) {
            // Continue parsing HTTP version.
            _httpVersionIndex++;
          } else if (_httpVersionIndex == _Const.HTTP11.length &&
                     byte == _CharCode.SP) {
            // HTTP version parsed.
            _state = _State.RESPONSE_LINE_STATUS_CODE;
          } else {
            // Did not parse HTTP version. Expect method instead.
            for (int i = 0; i < _httpVersionIndex; i++) {
              _method_or_status_code.addCharCode(_Const.HTTP11[i]);
            }
            _state = _State.REQUEST_LINE_URI;
          }
          break;

        case _State.REQUEST_LINE_METHOD:
          if (byte == _CharCode.SP) {
            _state = _State.REQUEST_LINE_URI;
          } else {
            _method_or_status_code.addCharCode(byte);
          }
          break;

        case _State.REQUEST_LINE_URI:
          if (byte == _CharCode.SP) {
            _state = _State.REQUEST_LINE_HTTP_VERSION;
            _httpVersionIndex = 0;
          } else {
            _uri_or_reason_phrase.addCharCode(byte);
          }
          break;

        case _State.REQUEST_LINE_HTTP_VERSION:
          if (_httpVersionIndex < _Const.HTTP11.length) {
            _expect(byte, _Const.HTTP11[_httpVersionIndex]);
            _httpVersionIndex++;
          } else {
            _expect(byte, _CharCode.CR);
            _state = _State.REQUEST_LINE_ENDING;
          }
          break;

        case _State.REQUEST_LINE_ENDING:
          _expect(byte, _CharCode.LF);
          if (requestStart != null) {
            requestStart(_method_or_status_code.toString(),
                         _uri_or_reason_phrase.toString());
          }
          _method_or_status_code.clear();
          _uri_or_reason_phrase.clear();
          _state = _State.HEADER_START;
          break;

        case _State.RESPONSE_LINE_STATUS_CODE:
          if (byte == _CharCode.SP) {
            _state = _State.RESPONSE_LINE_REASON_PHRASE;
          } else {
            if (byte < 0x30 && 0x39 < byte) {
              _failure = true;
            } else {
              _method_or_status_code.addCharCode(byte);
            }
          }
          break;

        case _State.RESPONSE_LINE_REASON_PHRASE:
          if (byte == _CharCode.CR) {
            _state = _State.RESPONSE_LINE_ENDING;
          } else {
            _uri_or_reason_phrase.addCharCode(byte);
          }
          break;

        case _State.RESPONSE_LINE_ENDING:
          _expect(byte, _CharCode.LF);
          // TODO(sgjesse): Check for valid status code.
          if (responseStart != null) {
            responseStart(Math.parseInt(_method_or_status_code.toString()),
                          _uri_or_reason_phrase.toString());
          }
          _method_or_status_code.clear();
          _uri_or_reason_phrase.clear();
          _state = _State.HEADER_START;
          break;

        case _State.HEADER_START:
          if (byte == _CharCode.CR) {
            _state = _State.HEADER_ENDING;
          } else {
            // Start of new header field.
            _headerField.addCharCode(_toLowerCase(byte));
            _state = _State.HEADER_FIELD;
          }
          break;

        case _State.HEADER_FIELD:
          if (byte == _CharCode.COLON) {
            _state = _State.HEADER_VALUE_START;
          } else {
            _headerField.addCharCode(_toLowerCase(byte));
          }
          break;

        case _State.HEADER_VALUE_START:
          if (byte != _CharCode.SP && byte != _CharCode.HT) {
            // Start of new header value.
            _headerValue.addCharCode(byte);
            _state = _State.HEADER_VALUE;
          }
          break;

        case _State.HEADER_VALUE:
          if (byte == _CharCode.CR) {
            _state = _State.HEADER_VALUE_FOLDING_OR_ENDING;
          } else {
            _headerValue.addCharCode(byte);
          }
          break;

        case _State.HEADER_VALUE_FOLDING_OR_ENDING:
          _expect(byte, _CharCode.LF);
          _state = _State.HEADER_VALUE_FOLD_OR_END;
          break;

        case _State.HEADER_VALUE_FOLD_OR_END:
          if (byte == _CharCode.SP || byte == _CharCode.HT) {
            _state = _State.HEADER_VALUE_START;
          } else {
            String headerField = _headerField.toString();
            String headerValue =_headerValue.toString();
            // Ignore the Content-Length header if Transfer-Encoding
            // is chunked (RFC 2616 section 4.4)
            if (headerField == "content-length" && !_chunked) {
              _contentLength = Math.parseInt(headerValue);
            } else if (headerField == "connection" &&
                       headerValue == "keep-alive") {
              _keepAlive = true;
            } else if (headerField == "transfer-encoding" &&
                       headerValue == "chunked") {
              _chunked = true;
              _contentLength = -1;
            }
            if (headerReceived != null) {
              headerReceived(headerField, headerValue);
            }
            _headerField.clear();
            _headerValue.clear();

            if (byte == _CharCode.CR) {
              _state = _State.HEADER_ENDING;
            } else {
              // Start of new header field.
              _headerField.addCharCode(_toLowerCase(byte));
              _state = _State.HEADER_FIELD;
            }
          }
          break;

        case _State.HEADER_ENDING:
          _expect(byte, _CharCode.LF);
          if (headersComplete != null) headersComplete();

          // If there is no data get ready to process the next request.
          if (_chunked) {
            _state = _State.CHUNK_SIZE;
            _remainingContent = 0;
          } else if (_contentLength == 0) {
            if (dataEnd != null) dataEnd();
            _state = _State.START;
          } else if (_contentLength > 0) {
            _remainingContent = _contentLength;
            _state = _State.BODY;
          } else {
            // TODO(sgjesse): Error handling.
          }
          break;

        case _State.CHUNK_SIZE_STARTING_CR:
          _expect(byte, _CharCode.CR);
          _state = _State.CHUNK_SIZE_STARTING_LF;
          break;

        case _State.CHUNK_SIZE_STARTING_LF:
          _expect(byte, _CharCode.LF);
          _state = _State.CHUNK_SIZE;
          break;

        case _State.CHUNK_SIZE:
          if (byte == _CharCode.CR) {
            _state = _State.CHUNK_SIZE_ENDING;
          } else {
            int value = _expectHexDigit(byte);
            _remainingContent = _remainingContent * 16 + value;
          }
          break;

        case _State.CHUNK_SIZE_ENDING:
          _expect(byte, _CharCode.LF);
          if (_remainingContent > 0) {
            _state = _State.BODY;
          } else {
            _state = _State.CHUNKED_BODY_DONE_CR;
          }
          break;

        case _State.CHUNKED_BODY_DONE_CR:
          _expect(byte, _CharCode.CR);
          _state = _State.CHUNKED_BODY_DONE_LF;
          break;

        case _State.CHUNKED_BODY_DONE_LF:
          _expect(byte, _CharCode.LF);
          if (dataEnd != null) dataEnd();
          _state = _State.START;
          break;

        case _State.BODY:
          // The body is not handled one byte at the time but in blocks.
          int dataAvailable = lastIndex - index;
          ByteArray data;
          if (dataAvailable <= _remainingContent) {
            data = new ByteArray(dataAvailable);
            data.setRange(0, dataAvailable, buffer, index);
          } else {
            data = new ByteArray(_remainingContent);
            data.setRange(0, _remainingContent, buffer, index);
          }

          if (dataReceived != null) dataReceived(data);
          _remainingContent -= data.length;
          index += data.length;
          if (_remainingContent == 0) {
            if (!_chunked) {
              if (dataEnd != null) dataEnd();
              _state = _State.START;
            } else {
              _state = _State.CHUNK_SIZE_STARTING_CR;
            }
          }

          // Hack - as we always do index++ below.
          index--;
          break;

        default:
          // Should be unreachable.
          assert(false);
      }

      // Move to the next byte.
      index++;
    }

    // Return the number of bytes parsed.
    return index - offset;
  }

  int get contentLength() => _contentLength;
  bool get keepAlive() => _keepAlive;

  int _toLowerCase(int byte) {
    final int aCode = "A".charCodeAt(0);
    final int zCode = "Z".charCodeAt(0);
    final int delta = "a".charCodeAt(0) - aCode;
    return (aCode <= byte && byte <= zCode) ? byte + delta : byte;
  }

  int _expect(int val1, int val2) {
    if (val1 != val2) {
      _failure = true;
    }
  }

  int _expectHexDigit(int byte) {
    if (0x30 <= byte && byte <= 0x39) {
      return byte - 0x30;  // 0 - 9
    } else if (0x41 <= byte && byte <= 0x46) {
      return byte - 0x41 + 10;  // A - F
    } else if (0x61 <= byte && byte <= 0x66) {
      return byte - 0x61 + 10;  // a - f
    } else {
      _failure = true;
      return 0;
    }
  }

  int _state;
  bool _failure;
  int _httpVersionIndex;
  StringBuffer _method_or_status_code;
  StringBuffer _uri_or_reason_phrase;
  StringBuffer _headerField;
  StringBuffer _headerValue;

  int _contentLength;
  bool _keepAlive;
  bool _chunked;

  int _remainingContent;

  // Callbacks.
  Function requestStart;
  Function responseStart;
  Function headerReceived;
  Function headersComplete;
  Function dataReceived;
  Function dataEnd;
}
