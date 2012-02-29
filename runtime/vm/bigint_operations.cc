// Copyright 2012 Google Inc. All Rights Reserved.

#include "vm/bigint_operations.h"

#include "platform/utils.h"

#include "vm/double_internals.h"
#include "vm/exceptions.h"
#include "vm/zone.h"

namespace dart {

RawBigint* BigintOperations::NewFromSmi(const Smi& smi, Heap::Space space) {
  intptr_t value = smi.Value();
  if (value == 0) {
    return Zero();
  }

  bool is_negative = (value < 0);
  if (is_negative) {
    value = -value;
  }
  // Assert that there are no overflows. Smis reserve a bit for themselves, but
  // protect against future changes.
  ASSERT(-Smi::kMinValue > 0);

  // A single digit of a Bigint might not be sufficient to store a Smi.
  // Count number of needed Digits.
  intptr_t digit_count = 0;
  intptr_t count_value = value;
  while (count_value > 0) {
    digit_count++;
    count_value >>= kDigitBitSize;
  }

  // Allocate a bigint of the correct size and copy the bits.
  const Bigint& result = Bigint::Handle(Bigint::Allocate(digit_count, space));
  for (int i = 0; i < digit_count; i++) {
    result.SetChunkAt(i, static_cast<Chunk>(value & kDigitMask));
    value >>= kDigitBitSize;
  }
  result.SetSign(is_negative);
  ASSERT(IsClamped(result));
  return result.raw();
}


RawBigint* BigintOperations::NewFromInt64(int64_t value, Heap::Space space) {
  bool is_negative = value < 0;

  if (is_negative) {
    value = -value;
  }

  const Bigint& result = Bigint::Handle(NewFromUint64(value, space));
  result.SetSign(is_negative);

  return result.raw();
}


RawBigint* BigintOperations::NewFromUint64(uint64_t value, Heap::Space space) {
  if (value == 0) {
    return Zero();
  }
  // A single digit of a Bigint might not be sufficient to store the value.
  // Count number of needed Digits.
  intptr_t digit_count = 0;
  uint64_t count_value = value;
  while (count_value > 0) {
    digit_count++;
    count_value >>= kDigitBitSize;
  }

  // Allocate a bigint of the correct size and copy the bits.
  const Bigint& result = Bigint::Handle(Bigint::Allocate(digit_count, space));
  for (int i = 0; i < digit_count; i++) {
    result.SetChunkAt(i, static_cast<Chunk>(value & kDigitMask));
    value >>= kDigitBitSize;
  }
  result.SetSign(false);
  ASSERT(IsClamped(result));
  return result.raw();
}


RawBigint* BigintOperations::NewFromCString(const char* str,
                                            Heap::Space space) {
  ASSERT(str != NULL);
  if (str[0] == '\0') {
    return Zero();
  }

  // If the string starts with '-' recursively restart the whole operation
  // without the character and then toggle the sign.
  // This allows multiple leading '-' (which will cancel each other out), but
  // we have added an assert, to make sure that the returned result of the
  // recursive call is not negative.
  // We don't catch leading '-'s for zero. Ex: "--0", or "---".
  if (str[0] == '-') {
    const Bigint& result = Bigint::Handle(NewFromCString(&str[1], space));
    result.ToggleSign();
    ASSERT(result.IsZero() || result.IsNegative());
    ASSERT(IsClamped(result));
    return result.raw();
  }

  intptr_t str_length = strlen(str);
  if ((str_length > 2) &&
      (str[0] == '0') &&
      ((str[1] == 'x') || (str[1] == 'X'))) {
    const Bigint& result = Bigint::Handle(FromHexCString(&str[2], space));
    ASSERT(IsClamped(result));
    return result.raw();
  } else {
    return FromDecimalCString(str);
  }
}


RawBigint* BigintOperations::FromHexCString(const char* hex_string,
                                            Heap::Space space) {
  // If the string starts with '-' recursively restart the whole operation
  // without the character and then toggle the sign.
  // This allows multiple leading '-' (which will cancel each other out), but
  // we have added an assert, to make sure that the returned result of the
  // recursive call is not negative.
  // We don't catch leading '-'s for zero. Ex: "--0", or "---".
  if (hex_string[0] == '-') {
    const Bigint& value = Bigint::Handle(FromHexCString(&hex_string[1], space));
    value.ToggleSign();
    ASSERT(value.IsZero() || value.IsNegative());
    ASSERT(IsClamped(value));
    return value.raw();
  }

  ASSERT(kDigitBitSize % 4 == 0);
  const int kHexCharsPerDigit = kDigitBitSize / 4;

  intptr_t hex_length = strlen(hex_string);
  // Round up.
  intptr_t bigint_length = ((hex_length - 1) / kHexCharsPerDigit) + 1;
  const Bigint& result =
      Bigint::Handle(Bigint::Allocate(bigint_length, space));
  // The bigint's least significant digit (lsd) is at position 0, whereas the
  // given string has it's lsd at the last position.
  // The hex_i index, pointing into the string, starts therefore at the end,
  // whereas the bigint-index (i) starts at 0.
  intptr_t hex_i = hex_length - 1;
  for (intptr_t i = 0; i < bigint_length; i++) {
    Chunk digit = 0;
    int shift = 0;
    for (int j = 0; j < kHexCharsPerDigit; j++) {
      // Reads a block of hexadecimal digits and stores it in 'digit'.
      // Ex: "0123456" with kHexCharsPerDigit == 3, hex_i == 6, reads "456".
      if (hex_i < 0) {
        break;
      }
      ASSERT(hex_i >= 0);
      char c = hex_string[hex_i--];
      ASSERT(Utils::IsHexDigit(c));
      digit += static_cast<Chunk>(Utils::HexDigitToInt(c)) << shift;
      shift += 4;
    }
    result.SetChunkAt(i, digit);
  }
  ASSERT(hex_i == -1);
  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::FromDecimalCString(const char* str,
                                                Heap::Space space) {
  // Read 8 digits a time. 10^8 < 2^27.
  const int kDigitsPerIteration = 8;
  const Chunk kTenMultiplier = 100000000;
  ASSERT(kDigitBitSize >= 27);

  intptr_t str_length = strlen(str);
  intptr_t str_pos = 0;

  // Read first digit separately. This avoids a multiplication and addition.
  // The first digit might also not have kDigitsPerIteration decimal digits.
  int first_digit_decimal_digits = str_length % kDigitsPerIteration;
  Chunk digit = 0;
  for (intptr_t i = 0; i < first_digit_decimal_digits; i++) {
    char c = str[str_pos++];
    ASSERT(('0' <= c) && (c <= '9'));
    digit = digit * 10 + c - '0';
  }
  Bigint& result = Bigint::Handle(Bigint::Allocate(1, space));
  result.SetChunkAt(0, digit);
  Clamp(result);  // Multiplication requires the inputs to be clamped.

  // Read kDigitsPerIteration at a time, and store it in 'increment'.
  // Then multiply the temporary result by 10^kDigitsPerIteration and add
  // 'increment' to the new result.
  const Bigint& increment = Bigint::Handle(Bigint::Allocate(1, space));
  while (str_pos < str_length - 1) {
    Chunk digit = 0;
    for (intptr_t i = 0; i < kDigitsPerIteration; i++) {
      char c = str[str_pos++];
      ASSERT(('0' <= c) && (c <= '9'));
      digit = digit * 10 + c - '0';
    }
    result ^= MultiplyWithDigit(result, kTenMultiplier);
    if (digit != 0) {
      increment.SetChunkAt(0, digit);
      result ^= Add(result, increment);
    }
  }
  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::NewFromDouble(double d, Heap::Space space) {
  if ((-1.0 < d) && (d < 1.0)) {
    // Shortcut for small numbers. Also makes the right-shift below
    // well specified.
    Smi& zero = Smi::Handle(Smi::New(0));
    return NewFromSmi(zero, space);
  }
  DoubleInternals internals = DoubleInternals(d);
  if (internals.IsSpecial()) {
    GrowableArray<const Object*> exception_arguments;
    exception_arguments.Add(
        &Object::ZoneHandle(String::New("BigintOperations::NewFromDouble")));
    Exceptions::ThrowByType(Exceptions::kInternalError, exception_arguments);
  }
  uint64_t significand = internals.Significand();
  int exponent = internals.Exponent();
  int sign = internals.Sign();
  if (exponent <= 0) {
    significand >>= -exponent;
    exponent = 0;
  } else if (exponent <= 10) {
    // A double significand has at most 53 bits. The following shift will
    // hence not overflow, and yield an integer of at most 63 bits.
    significand <<= exponent;
    exponent = 0;
  }
  // A significand has at most 63 bits (after the shift above).
  // The cast to int64_t is hence safe.
  const Bigint& result =
      Bigint::Handle(NewFromInt64(static_cast<int64_t>(significand), space));
  result.SetSign(sign < 0);
  if (exponent > 0) {
    return ShiftLeft(result, exponent);
  } else {
    return result.raw();
  }
}


const char* BigintOperations::ToHexCString(intptr_t length,
                                           bool is_negative,
                                           void* data,
                                           uword (*allocator)(intptr_t size)) {
  NoGCScope no_gc;

  ASSERT(kDigitBitSize % 4 == 0);
  const int kHexCharsPerDigit = kDigitBitSize / 4;

  intptr_t chunk_length = length;
  Chunk* chunk_data = reinterpret_cast<Chunk*>(data);
  if (length == 0) {
    const char* zero = "0x0";
    const int kLength = strlen(zero);
    char* result = reinterpret_cast<char*>(allocator(kLength + 1));
    ASSERT(result != NULL);
    memmove(result, zero, kLength);
    result[kLength] = '\0';
    return result;
  }
  ASSERT(chunk_data != NULL);

  // Compute the number of hex-digits that are needed to represent the
  // leading bigint-digit. All other digits need exactly kHexCharsPerDigit
  // characters.
  int leading_hex_digits = 0;
  Chunk leading_digit = chunk_data[chunk_length - 1];
  while (leading_digit != 0) {
    leading_hex_digits++;
    leading_digit >>= 4;
  }
  // Sum up the space that is needed for the string-representation.
  intptr_t required_size = 0;
  if (is_negative) {
    required_size++;  // For the leading "-".
  }
  required_size += 2;  // For the "0x".
  required_size += leading_hex_digits;
  required_size += (chunk_length - 1) * kHexCharsPerDigit;
  required_size++;  // For the trailing '\0'.
  char* result = reinterpret_cast<char*>(allocator(required_size));
  // Print the number into the string.
  // Start from the last position.
  intptr_t pos = required_size - 1;
  result[pos--] = '\0';
  for (intptr_t i = 0; i < (chunk_length - 1); i++) {
    // Print all non-leading characters (which are printed with
    // kHexCharsPerDigit characters.
    Chunk digit = chunk_data[i];
    for (int j = 0; j < kHexCharsPerDigit; j++) {
      result[pos--] = Utils::IntToHexDigit(static_cast<int>(digit & 0xF));
      digit >>= 4;
    }
  }
  // Print the leading digit.
  leading_digit = chunk_data[chunk_length - 1];
  while (leading_digit != 0) {
    result[pos--] = Utils::IntToHexDigit(static_cast<int>(leading_digit & 0xF));
    leading_digit >>= 4;
  }
  result[pos--] = 'x';
  result[pos--] = '0';
  if (is_negative) {
    result[pos--] = '-';
  }
  ASSERT(pos == -1);
  return result;
}


const char* BigintOperations::ToHexCString(const Bigint& bigint,
                                           uword (*allocator)(intptr_t size)) {
  NoGCScope no_gc;

  intptr_t length = bigint.Length();
  return ToHexCString(length,
                      bigint.IsNegative(),
                      length ? bigint.ChunkAddr(0) : NULL,
                      allocator);
}


bool BigintOperations::FitsIntoSmi(const Bigint& bigint) {
  intptr_t bigint_length = bigint.Length();
  if (bigint_length == 0) {
    return true;
  }
  if ((bigint_length == 1) &&
      (static_cast<size_t>(kDigitBitSize) <
       (sizeof(intptr_t) * kBitsPerByte))) {
    return true;
  }

  uintptr_t limit;
  if (bigint.IsNegative()) {
    limit = static_cast<uintptr_t>(-Smi::kMinValue);
  } else {
    limit = static_cast<uintptr_t>(Smi::kMaxValue);
  }
  bool bigint_is_greater = false;
  // Consume the least-significant digits of the bigint.
  // If bigint_is_greater is set, then the processed sub-part of the bigint is
  // greater than the corresponding part of the limit.
  for (int i = 0; i < bigint_length - 1; i++) {
    Chunk limit_digit = static_cast<Chunk>(limit & kDigitMask);
    Chunk bigint_digit = bigint.GetChunkAt(i);
    if (limit_digit < bigint_digit) {
      bigint_is_greater = true;
    } else if (limit_digit > bigint_digit) {
      bigint_is_greater = false;
    }  // else don't change the boolean.
    limit >>= kDigitBitSize;

    // Bail out if the bigint is definitely too big.
    if (limit == 0) {
      return false;
    }
  }
  Chunk most_significant_digit = bigint.GetChunkAt(bigint_length - 1);
  if (limit > most_significant_digit) {
    return true;
  }
  if (limit < most_significant_digit) {
    return false;
  }
  return !bigint_is_greater;
}


RawSmi* BigintOperations::ToSmi(const Bigint& bigint) {
  ASSERT(FitsIntoSmi(bigint));
  intptr_t value = 0;
  for (int i = bigint.Length() - 1; i >= 0; i--) {
    value <<= kDigitBitSize;
    value += static_cast<intptr_t>(bigint.GetChunkAt(i));
  }
  if (bigint.IsNegative()) {
    value = -value;
  }
  return Smi::New(value);
}


RawDouble* BigintOperations::ToDouble(const Bigint& bigint) {
  // TODO(floitsch/benl): This is a quick and dirty implementation to unblock
  // other areas of the code. It does not handle all bit-twiddling correctly.
  const double shift_value = (1 << kDigitBitSize);
  double value = 0.0;
  for (int i = bigint.Length() - 1; i >= 0; i--) {
    value *= shift_value;
    value += static_cast<double>(bigint.GetChunkAt(i));
  }
  if (bigint.IsNegative()) {
    value = -value;
  }
  return Double::New(value);
}


bool BigintOperations::FitsIntoMint(const Bigint& bigint) {
  intptr_t bigint_length = bigint.Length();
  if (bigint_length == 0) {
    return true;
  }
  if ((bigint_length < 3) &&
      (static_cast<size_t>(kDigitBitSize) <
       (sizeof(intptr_t) * kBitsPerByte))) {
    return true;
  }

  uint64_t limit;
  if (bigint.IsNegative()) {
    limit = static_cast<uint64_t>(Mint::kMinValue);
  } else {
    limit = static_cast<uint64_t>(Mint::kMaxValue);
  }
  bool bigint_is_greater = false;
  // Consume the least-significant digits of the bigint.
  // If bigint_is_greater is set, then the processed sub-part of the bigint is
  // greater than the corresponding part of the limit.
  for (int i = 0; i < bigint_length - 1; i++) {
    Chunk limit_digit = static_cast<Chunk>(limit & kDigitMask);
    Chunk bigint_digit = bigint.GetChunkAt(i);
    if (limit_digit < bigint_digit) {
      bigint_is_greater = true;
    } else if (limit_digit > bigint_digit) {
      bigint_is_greater = false;
    }  // else don't change the boolean.
    limit >>= kDigitBitSize;

    // Bail out if the bigint is definitely too big.
    if (limit == 0) {
      return false;
    }
  }
  Chunk most_significant_digit = bigint.GetChunkAt(bigint_length - 1);
  if (limit > most_significant_digit) {
    return true;
  }
  if (limit < most_significant_digit) {
    return false;
  }
  return !bigint_is_greater;
}


uint64_t BigintOperations::AbsToUint64(const Bigint& bigint) {
  uint64_t value = 0;
  for (int i = bigint.Length() - 1; i >= 0; i--) {
    value <<= kDigitBitSize;
    value += static_cast<intptr_t>(bigint.GetChunkAt(i));
  }
  return value;
}


int64_t BigintOperations::ToMint(const Bigint& bigint) {
  ASSERT(FitsIntoMint(bigint));
  int64_t value = AbsToUint64(bigint);
  if (bigint.IsNegative()) {
    value = -value;
  }
  return value;
}


bool BigintOperations::FitsIntoUint64(const Bigint& bigint) {
  if (bigint.IsNegative()) return false;
  intptr_t b_length = bigint.Length();
  int num_bits = CountBits(bigint.GetChunkAt(b_length - 1));
  num_bits += (kDigitBitSize * (b_length - 1));
  if (num_bits > 64) return false;
  return true;
}


uint64_t BigintOperations::ToUint64(const Bigint& bigint) {
  ASSERT(FitsIntoUint64(bigint));
  return AbsToUint64(bigint);
}


RawBigint* BigintOperations::Multiply(const Bigint& a, const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));

  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  intptr_t result_length = a_length + b_length;
  const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));

  if (a.IsNegative() != b.IsNegative()) {
    result.ToggleSign();
  }

  // Comba multiplication: compute each column separately.
  // Example: r = a2a1a0 * b2b1b0.
  //    r =  1    * a0b0 +
  //        10    * (a1b0 + a0b1) +
  //        100   * (a2b0 + a1b1 + a0b2) +
  //        1000  * (a2b1 + a1b2) +
  //        10000 * a2b2
  //
  // Each column will be accumulated in an integer of type DoubleChunk. We
  // must guarantee that the column-sum will not overflow.
  //
  // In the worst case we have to accumulate k = Min(a.length, b.length)
  // products plus the carry from the previous round.
  // Each bigint-digit is smaller than beta = 2^kDigitBitSize.
  // Each product is at most (beta - 1)^2.
  // If we want to use Comba multiplication the following condition must hold:
  // k * (beta - 1)^2 + (2^(kDoubleChunkBitSize - kDigitBitSize) - 1) <
  //        2^kDoubleChunkBitSize.
  const DoubleChunk square =
      static_cast<DoubleChunk>(kDigitMaxValue) * kDigitMaxValue;
  const DoubleChunk kDoubleChunkMaxValue = static_cast<DoubleChunk>(-1);
  const DoubleChunk left_over_carry = kDoubleChunkMaxValue >> kDigitBitSize;
  const intptr_t kMaxDigits = (kDoubleChunkMaxValue - left_over_carry) / square;
  if (Utils::Minimum(a_length, b_length) > kMaxDigits) {
    UNIMPLEMENTED();
  }

  DoubleChunk accumulator = 0;  // Accumulates the result of one column.
  for (intptr_t i = 0; i < result_length; i++) {
    // Example: r = a2a1a0 * b2b1b0.
    //   For i == 0, compute a0b0.
    //       i == 1,         a1b0 + a0b1 + overflow from i == 0.
    //       i == 2,         a2b0 + a1b1 + a0b2 + overflow from i == 1.
    //       ...
    // The indices into a and b are such that their sum equals i.
    intptr_t a_index = Utils::Minimum(a_length - 1, i);
    intptr_t b_index = i - a_index;
    ASSERT(a_index + b_index == i);

    // Instead of testing for a_index >= 0 && b_index < b_length we compute the
    // number of iterations first.
    intptr_t iterations = Utils::Minimum(b_length - b_index, a_index + 1);
    for (intptr_t j = 0; j < iterations; j++) {
      DoubleChunk chunk_a = a.GetChunkAt(a_index);
      DoubleChunk chunk_b = b.GetChunkAt(b_index);
      accumulator += chunk_a * chunk_b;
      a_index--;
      b_index++;
    }
    result.SetChunkAt(i, static_cast<Chunk>(accumulator & kDigitMask));
    accumulator >>= kDigitBitSize;
  }
  ASSERT(accumulator == 0);

  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::Divide(const Bigint& a, const Bigint& b) {
  Bigint& quotient = Bigint::Handle();
  Bigint& remainder = Bigint::Handle();
  DivideRemainder(a, b, &quotient, &remainder);
  return quotient.raw();
}


RawBigint* BigintOperations::Modulo(const Bigint& a, const Bigint& b) {
  Bigint& quotient = Bigint::Handle();
  Bigint& modulo = Bigint::Handle();
  DivideRemainder(a, b, &quotient, &modulo);
  return modulo.raw();
}


RawBigint* BigintOperations::Remainder(const Bigint& a, const Bigint& b) {
  Bigint& quotient = Bigint::Handle();
  Bigint& remainder = Bigint::Handle();
  DivideRemainder(a, b, &quotient, &remainder);
  return remainder.raw();
}


RawBigint* BigintOperations::ShiftLeft(const Bigint& bigint, intptr_t amount) {
  ASSERT(IsClamped(bigint));
  ASSERT(amount >= 0);
  intptr_t bigint_length = bigint.Length();
  if (bigint.IsZero()) {
    return Zero();
  }
  // TODO(floitsch): can we reuse the input?
  if (amount == 0) {
    return Copy(bigint);
  }
  intptr_t digit_shift = amount / kDigitBitSize;
  intptr_t bit_shift = amount % kDigitBitSize;
  if (bit_shift == 0) {
    const Bigint& result =
        Bigint::Handle(Bigint::Allocate(bigint_length + digit_shift));
    for (intptr_t i = 0; i < digit_shift; i++) {
      result.SetChunkAt(i, 0);
    }
    for (intptr_t i = 0; i < bigint_length; i++) {
      result.SetChunkAt(i + digit_shift, bigint.GetChunkAt(i));
    }
    if (bigint.IsNegative()) {
      result.ToggleSign();
    }
    return result.raw();
  } else {
    const Bigint& result =
        Bigint::Handle(Bigint::Allocate(bigint_length + digit_shift + 1));
    for (intptr_t i = 0; i < digit_shift; i++) {
      result.SetChunkAt(i, 0);
    }
    Chunk carry = 0;
    for (intptr_t i = 0; i < bigint_length; i++) {
      Chunk digit = bigint.GetChunkAt(i);
      Chunk shifted_digit = ((digit << bit_shift) & kDigitMask) + carry;
      result.SetChunkAt(i + digit_shift, shifted_digit);
      carry = digit >> (kDigitBitSize - bit_shift);
    }
    result.SetChunkAt(bigint_length + digit_shift, carry);
    if (bigint.IsNegative()) {
      result.ToggleSign();
    }
    Clamp(result);
    return result.raw();
  }
}


RawBigint* BigintOperations::ShiftRight(const Bigint& bigint, intptr_t amount) {
  ASSERT(IsClamped(bigint));
  ASSERT(amount >= 0);
  intptr_t bigint_length = bigint.Length();
  if (bigint.IsZero()) {
    return Zero();
  }
  // TODO(floitsch): can we reuse the input?
  if (amount == 0) {
    return Copy(bigint);
  }
  intptr_t digit_shift = amount / kDigitBitSize;
  intptr_t bit_shift = amount % kDigitBitSize;
  if (digit_shift >= bigint_length) {
    return bigint.IsNegative() ? MinusOne() : Zero();
  }

  const Bigint& result =
      Bigint::Handle(Bigint::Allocate(bigint_length - digit_shift));
  if (bit_shift == 0) {
    for (intptr_t i = 0; i < bigint_length - digit_shift; i++) {
      result.SetChunkAt(i, bigint.GetChunkAt(i + digit_shift));
    }
  } else {
    Chunk carry = 0;
    for (intptr_t i = bigint_length - 1; i >= digit_shift; i--) {
      Chunk digit = bigint.GetChunkAt(i);
      Chunk shifted_digit = (digit >> bit_shift) + carry;
      result.SetChunkAt(i - digit_shift, shifted_digit);
      carry = (digit << (kDigitBitSize - bit_shift)) & kDigitMask;
    }
    Clamp(result);
  }

  if (bigint.IsNegative()) {
    result.ToggleSign();
    // If the input is negative then the result needs to be rounded down.
    // Example: -5 >> 2 => -2
    bool needs_rounding = false;
    for (intptr_t i = 0; i < digit_shift; i++) {
      if (bigint.GetChunkAt(i) != 0) {
        needs_rounding = true;
        break;
      }
    }
    if (!needs_rounding && (bit_shift > 0)) {
      Chunk digit = bigint.GetChunkAt(digit_shift);
      needs_rounding = (digit << (kChunkBitSize - bit_shift)) != 0;
    }
    if (needs_rounding) {
      Bigint& one = Bigint::Handle(One());
      return Subtract(result, one);
    }
  }

  return result.raw();
}


RawBigint* BigintOperations::BitAnd(const Bigint& a, const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));

  if (a.IsZero() || b.IsZero()) {
    return Zero();
  }
  if (a.IsNegative() && !b.IsNegative()) {
    return BitAnd(b, a);
  }
  if ((a.IsNegative() == b.IsNegative()) && (a.Length() < b.Length())) {
    return BitAnd(b, a);
  }

  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  intptr_t min_length = Utils::Minimum(a_length, b_length);
  intptr_t max_length = Utils::Maximum(a_length, b_length);
  if (!b.IsNegative()) {
    ASSERT(!a.IsNegative());
    intptr_t result_length = min_length;
    const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));

    for (intptr_t i = 0; i < min_length; i++) {
      result.SetChunkAt(i, a.GetChunkAt(i) & b.GetChunkAt(i));
    }
    Clamp(result);
    return result.raw();
  }

  // Bigints encode negative values by storing the absolute value and the sign
  // separately. To do bit operations we need to simulate numbers that are
  // implemented as two's complement.
  // The negation of a positive number x would be encoded as follows in
  // two's complement: n = ~(x - 1).
  // The inverse transformation is hence (~n) + 1.

  if (!a.IsNegative()) {
    ASSERT(b.IsNegative());
    // The result will be positive.
    intptr_t result_length = a_length;
    const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));
    Chunk borrow = 1;
    for (intptr_t i = 0; i < min_length; i++) {
      Chunk b_digit = b.GetChunkAt(i) - borrow;
      result.SetChunkAt(i, a.GetChunkAt(i) & (~b_digit) & kDigitMask);
      borrow = b_digit >> (kChunkBitSize - 1);
    }
    for (intptr_t i = min_length; i < a_length; i++) {
      result.SetChunkAt(i, a.GetChunkAt(i) & (kDigitMaxValue - borrow));
      borrow = 0;
    }
    Clamp(result);
    return result.raw();
  }

  ASSERT(a.IsNegative());
  ASSERT(b.IsNegative());
  // The result will be negative.
  // We need to convert a and b to two's complement. Do the bit-operation there,
  // and transform the resulting bits from two's complement back to separated
  // magnitude and sign.
  // a & b is therefore computed as ~((~(a - 1)) & (~(b - 1))) + 1 which is
  //   equal to ((a-1) | (b-1)) + 1.
  intptr_t result_length = max_length + 1;
  const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));
  result.ToggleSign();
  Chunk a_borrow = 1;
  Chunk b_borrow = 1;
  Chunk result_carry = 1;
  ASSERT(a_length >= b_length);
  for (intptr_t i = 0; i < b_length; i++) {
    Chunk a_digit = a.GetChunkAt(i) - a_borrow;
    Chunk b_digit = b.GetChunkAt(i) - b_borrow;
    Chunk result_chunk = ((a_digit | b_digit) & kDigitMask) + result_carry;
    result.SetChunkAt(i, result_chunk & kDigitMask);
    a_borrow = a_digit >> (kChunkBitSize - 1);
    b_borrow = b_digit >> (kChunkBitSize - 1);
    result_carry = result_chunk >> kDigitBitSize;
  }
  for (intptr_t i = b_length; i < a_length; i++) {
    Chunk a_digit = a.GetChunkAt(i) - a_borrow;
    Chunk b_digit = -b_borrow;
    Chunk result_chunk = ((a_digit | b_digit) & kDigitMask) + result_carry;
    result.SetChunkAt(i, result_chunk & kDigitMask);
    a_borrow = a_digit >> (kChunkBitSize - 1);
    b_borrow = 0;
    result_carry = result_chunk >> kDigitBitSize;
  }
  Chunk a_digit = -a_borrow;
  Chunk b_digit = -b_borrow;
  Chunk result_chunk = ((a_digit | b_digit) & kDigitMask) + result_carry;
  result.SetChunkAt(a_length, result_chunk & kDigitMask);
  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::BitOr(const Bigint& a, const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));

  if (a.IsNegative() && !b.IsNegative()) {
    return BitOr(b, a);
  }
  if ((a.IsNegative() == b.IsNegative()) && (a.Length() < b.Length())) {
    return BitOr(b, a);
  }

  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  intptr_t min_length = Utils::Minimum(a_length, b_length);
  intptr_t max_length = Utils::Maximum(a_length, b_length);
  if (!b.IsNegative()) {
    ASSERT(!a.IsNegative());
    intptr_t result_length = max_length;
    const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));

    ASSERT(a_length >= b_length);
    for (intptr_t i = 0; i < b_length; i++) {
      result.SetChunkAt(i, a.GetChunkAt(i) | b.GetChunkAt(i));
    }
    for (intptr_t i = b_length; i < a_length; i++) {
      result.SetChunkAt(i, a.GetChunkAt(i));
    }
    return result.raw();
  }

  // Bigints encode negative values by storing the absolute value and the sign
  // separately. To do bit operations we need to simulate numbers that are
  // implemented as two's complement.
  // The negation of a positive number x would be encoded as follows in
  // two's complement: n = ~(x - 1).
  // The inverse transformation is hence (~n) + 1.

  if (!a.IsNegative()) {
    ASSERT(b.IsNegative());
    if (a.IsZero()) {
      return Copy(b);
    }
    // The result will be negative.
    // We need to convert  b to two's complement. Do the bit-operation there,
    // and transform the resulting bits from two's complement back to separated
    // magnitude and sign.
    // a | b is therefore computed as ~((a & (~(b - 1))) + 1 which is
    //   equal to ((~a) & (b-1)) + 1.
    intptr_t result_length = b_length;
    const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));
    result.ToggleSign();
    Chunk borrow = 1;
    Chunk result_carry = 1;
    for (intptr_t i = 0; i < min_length; i++) {
      Chunk a_digit = a.GetChunkAt(i);
      Chunk b_digit = b.GetChunkAt(i) - borrow;
      Chunk result_digit = ((~a_digit) & b_digit & kDigitMask) + result_carry;
      result.SetChunkAt(i, result_digit & kDigitMask);
      borrow = b_digit >> (kChunkBitSize - 1);
      result_carry = result_digit >> kDigitBitSize;
    }
    ASSERT(result_carry == 0);
    for (intptr_t i = min_length; i < b_length; i++) {
      Chunk b_digit = b.GetChunkAt(i) - borrow;
      Chunk result_digit = (b_digit & kDigitMask) + result_carry;
      result.SetChunkAt(i, result_digit & kDigitMask);
      borrow = b_digit >> (kChunkBitSize - 1);
      result_carry = result_digit >> kDigitBitSize;
    }
    ASSERT(result_carry == 0);
    Clamp(result);
    return result.raw();
  }

  ASSERT(a.IsNegative());
  ASSERT(b.IsNegative());
  // The result will be negative.
  // We need to convert a and b to two's complement. Do the bit-operation there,
  // and transform the resulting bits from two's complement back to separated
  // magnitude and sign.
  // a & b is therefore computed as ~((~(a - 1)) | (~(b - 1))) + 1 which is
  //   equal to ((a-1) & (b-1)) + 1.
  intptr_t result_length = min_length + 1;
  const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));
  result.ToggleSign();
  Chunk a_borrow = 1;
  Chunk b_borrow = 1;
  Chunk result_carry = 1;
  ASSERT(a_length >= b_length);
  for (intptr_t i = 0; i < b_length; i++) {
    Chunk a_digit = a.GetChunkAt(i) - a_borrow;
    Chunk b_digit = b.GetChunkAt(i) - b_borrow;
    Chunk result_chunk = ((a_digit & b_digit) & kDigitMask) + result_carry;
    result.SetChunkAt(i, result_chunk & kDigitMask);
    a_borrow = a_digit >> (kChunkBitSize - 1);
    b_borrow = b_digit >> (kChunkBitSize - 1);
    result_carry = result_chunk >> kDigitBitSize;
  }
  result.SetChunkAt(a_length, result_carry);
  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::BitXor(const Bigint& a, const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));

  if (a.IsZero()) {
    return Copy(b);
  }
  if (b.IsZero()) {
    return Copy(a);
  }
  if (a.IsNegative() && !b.IsNegative()) {
    return BitXor(b, a);
  }
  if ((a.IsNegative() == b.IsNegative()) && (a.Length() < b.Length())) {
    return BitXor(b, a);
  }

  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  intptr_t min_length = Utils::Minimum(a_length, b_length);
  intptr_t max_length = Utils::Maximum(a_length, b_length);
  if (!b.IsNegative()) {
    ASSERT(!a.IsNegative());
    intptr_t result_length = max_length;
    const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));

    ASSERT(a_length >= b_length);
    for (intptr_t i = 0; i < b_length; i++) {
      result.SetChunkAt(i, a.GetChunkAt(i) ^ b.GetChunkAt(i));
    }
    for (intptr_t i = b_length; i < a_length; i++) {
      result.SetChunkAt(i, a.GetChunkAt(i));
    }
    Clamp(result);
    return result.raw();
  }

  // Bigints encode negative values by storing the absolute value and the sign
  // separately. To do bit operations we need to simulate numbers that are
  // implemented as two's complement.
  // The negation of a positive number x would be encoded as follows in
  // two's complement: n = ~(x - 1).
  // The inverse transformation is hence (~n) + 1.

  if (!a.IsNegative()) {
    ASSERT(b.IsNegative());
    // The result will be negative.
    // We need to convert  b to two's complement. Do the bit-operation there,
    // and transform the resulting bits from two's complement back to separated
    // magnitude and sign.
    // a ^ b is therefore computed as ~((a ^ (~(b - 1))) + 1.
    intptr_t result_length = max_length + 1;
    const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));
    result.ToggleSign();
    Chunk borrow = 1;
    Chunk result_carry = 1;
    for (intptr_t i = 0; i < min_length; i++) {
      Chunk a_digit = a.GetChunkAt(i);
      Chunk b_digit = b.GetChunkAt(i) - borrow;
      Chunk result_digit =
          ((~(a_digit ^ ~b_digit)) & kDigitMask) + result_carry;
      result.SetChunkAt(i, result_digit & kDigitMask);
      borrow = b_digit >> (kChunkBitSize - 1);
      result_carry = result_digit >> kDigitBitSize;
    }
    for (intptr_t i = min_length; i < a_length; i++) {
      Chunk a_digit = a.GetChunkAt(i);
      Chunk b_digit = -borrow;
      Chunk result_digit =
          ((~(a_digit ^ ~b_digit)) & kDigitMask) + result_carry;
      result.SetChunkAt(i, result_digit & kDigitMask);
      borrow = b_digit >> (kChunkBitSize - 1);
      result_carry = result_digit >> kDigitBitSize;
    }
    for (intptr_t i = min_length; i < b_length; i++) {
      // a_digit = 0.
      Chunk b_digit = b.GetChunkAt(i) - borrow;
      Chunk result_digit = (b_digit & kDigitMask) + result_carry;
      result.SetChunkAt(i, result_digit & kDigitMask);
      borrow = b_digit >> (kChunkBitSize - 1);
      result_carry = result_digit >> kDigitBitSize;
    }
    result.SetChunkAt(max_length, result_carry);
    Clamp(result);
    return result.raw();
  }

  ASSERT(a.IsNegative());
  ASSERT(b.IsNegative());
  // The result will be positive.
  // We need to convert a and b to two's complement, do the bit-operation there,
  // and simply store the result.
  // a ^ b is therefore computed as (~(a - 1)) ^ (~(b - 1)).
  intptr_t result_length = max_length;
  const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));
  Chunk a_borrow = 1;
  Chunk b_borrow = 1;
  ASSERT(a_length >= b_length);
  for (intptr_t i = 0; i < b_length; i++) {
    Chunk a_digit = a.GetChunkAt(i) - a_borrow;
    Chunk b_digit = b.GetChunkAt(i) - b_borrow;
    Chunk result_chunk = (~a_digit) ^ (~b_digit);
    result.SetChunkAt(i, result_chunk & kDigitMask);
    a_borrow = a_digit >> (kChunkBitSize - 1);
    b_borrow = b_digit >> (kChunkBitSize - 1);
  }
  ASSERT(b_borrow == 0);
  for (intptr_t i = b_length; i < a_length; i++) {
    Chunk a_digit = a.GetChunkAt(i) - a_borrow;
    result.SetChunkAt(i, (~a_digit) & kDigitMask);
    a_borrow = a_digit >> (kChunkBitSize - 1);
  }
  ASSERT(a_borrow == 0);
  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::BitNot(const Bigint& bigint) {
  if (bigint.IsZero()) {
    return MinusOne();
  }
  const Bigint& one_bigint = Bigint::Handle(One());
  if (bigint.IsNegative()) {
    return UnsignedSubtract(bigint, one_bigint);
  } else {
    const Bigint& result = Bigint::Handle(UnsignedAdd(bigint, one_bigint));
    result.ToggleSign();
    return result.raw();
  }
}


int BigintOperations::Compare(const Bigint& a, const Bigint& b) {
  bool a_is_negative = a.IsNegative();
  bool b_is_negative = b.IsNegative();
  if (a_is_negative != b_is_negative) {
    return a_is_negative ? -1 : 1;
  }

  if (a_is_negative) {
    return -UnsignedCompare(a, b);
  }
  return UnsignedCompare(a, b);
}


RawBigint* BigintOperations::AddSubtract(const Bigint& a,
                                         const Bigint& b,
                                         bool negate_b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));
  Bigint& result = Bigint::Handle();
  // We perform the subtraction by simulating a negation of the b-argument.
  bool b_is_negative = negate_b ? !b.IsNegative() : b.IsNegative();

  // If both are of the same sign, then we can compute the unsigned addition
  // and then simply adjust the sign (if necessary).
  // Ex: -3 + -5 -> -(3 + 5)
  if (a.IsNegative() == b_is_negative) {
    result = UnsignedAdd(a, b);
    result.SetSign(b_is_negative);
    ASSERT(IsClamped(result));
    return result.raw();
  }

  // The signs differ.
  // Take the number with small magnitude and subtract its absolute value from
  // the absolute value of the other number. Then adjust the sign, if necessary.
  // The sign is the same as for the number with the greater magnitude.
  // Ex:  -8 + 3  -> -(8 - 3)
  //       8 + -3 ->  (8 - 3)
  //      -3 + 8  ->  (8 - 3)
  //       3 + -8 -> -(8 - 3)
  int comp = UnsignedCompare(a, b);
  if (comp < 0) {
    result = UnsignedSubtract(b, a);
    result.SetSign(b_is_negative);
  } else if (comp > 0) {
    result = UnsignedSubtract(a, b);
    result.SetSign(a.IsNegative());
  } else {
    return Zero();
  }
  ASSERT(IsClamped(result));
  return result.raw();
}


int BigintOperations::UnsignedCompare(const Bigint& a, const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));
  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  if (a_length < b_length) return -1;
  if (a_length > b_length) return 1;
  for (intptr_t i = a_length - 1; i >= 0; i--) {
    Chunk digit_a = a.GetChunkAt(i);
    Chunk digit_b = b.GetChunkAt(i);
    if (digit_a < digit_b) return -1;
    if (digit_a > digit_b) return 1;
    // Else look at the next digit.
  }
  return 0;  // They are equal.
}


int BigintOperations::UnsignedCompareNonClamped(
    const Bigint& a, const Bigint& b) {
  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  while (a_length > b_length) {
    if (a.GetChunkAt(a_length - 1) != 0) return 1;
    a_length--;
  }
  while (b_length > a_length) {
    if (b.GetChunkAt(b_length - 1) != 0) return -1;
    b_length--;
  }
  for (intptr_t i = a_length - 1; i >= 0; i--) {
    Chunk digit_a = a.GetChunkAt(i);
    Chunk digit_b = b.GetChunkAt(i);
    if (digit_a < digit_b) return -1;
    if (digit_a > digit_b) return 1;
    // Else look at the next digit.
  }
  return 0;  // They are equal.
}


RawBigint* BigintOperations::UnsignedAdd(const Bigint& a, const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));

  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();
  if (a_length < b_length) {
    return UnsignedAdd(b, a);
  }

  // We might request too much space, in which case we will adjust the length
  // afterwards.
  intptr_t result_length = a_length + 1;
  const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));

  Chunk carry = 0;
  // b has fewer digits than a.
  ASSERT(b_length <= a_length);
  for (intptr_t i = 0; i < b_length; i++) {
    Chunk sum = a.GetChunkAt(i) + b.GetChunkAt(i) + carry;
    result.SetChunkAt(i, sum & kDigitMask);
    carry = sum >> kDigitBitSize;
  }
  // Copy over the remaining digits of a, but don't forget the carry.
  for (intptr_t i = b_length; i < a_length; i++) {
    Chunk sum = a.GetChunkAt(i) + carry;
    result.SetChunkAt(i, sum & kDigitMask);
    carry = sum >> kDigitBitSize;
  }
  // Shrink the result if there was no overflow. Otherwise apply the carry.
  if (carry == 0) {
    // TODO(floitsch): We change the size of bigint-objects here.
    result.SetLength(a_length);
  } else {
    result.SetChunkAt(a_length, carry);
  }
  ASSERT(IsClamped(result));
  return result.raw();
}


RawBigint* BigintOperations::UnsignedSubtract(const Bigint& a,
                                              const Bigint& b) {
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));
  ASSERT(UnsignedCompare(a, b) >= 0);

  const int kSignBitPos = Bigint::kChunkSize * kBitsPerByte - 1;

  intptr_t a_length = a.Length();
  intptr_t b_length = b.Length();

  // We might request too much space, in which case we will adjust the length
  // afterwards.
  intptr_t result_length = a_length;
  const Bigint& result = Bigint::Handle(Bigint::Allocate(result_length));

  Chunk borrow = 0;
  ASSERT(b_length <= a_length);
  for (intptr_t i = 0; i < b_length; i++) {
    Chunk difference = a.GetChunkAt(i) - b.GetChunkAt(i) - borrow;
    result.SetChunkAt(i, difference & kDigitMask);
    borrow = difference >> kSignBitPos;
    ASSERT((borrow == 0) || (borrow == 1));
  }
  // Copy over the remaining digits of a, but don't forget the borrow.
  for (intptr_t i = b_length; i < a_length; i++) {
    Chunk difference = a.GetChunkAt(i) - borrow;
    result.SetChunkAt(i, difference & kDigitMask);
    borrow = (difference >> kSignBitPos);
    ASSERT((borrow == 0) || (borrow == 1));
  }
  ASSERT(borrow == 0);
  Clamp(result);
  return result.raw();
}


RawBigint* BigintOperations::MultiplyWithDigit(
    const Bigint& bigint, Chunk digit) {
  // TODO(floitsch): implement MultiplyWithDigit.
  ASSERT(digit <= kDigitMaxValue);
  if (digit == 0) return Zero();

  Bigint& tmp = Bigint::Handle(Bigint::Allocate(1));
  tmp.SetChunkAt(0, digit);
  return Multiply(bigint, tmp);
}


void BigintOperations::DivideRemainder(
    const Bigint& a, const Bigint& b, Bigint* quotient, Bigint* remainder) {
  // TODO(floitsch): This function is very memory-intensive since all
  // intermediate bigint results are allocated in new memory. It would be
  // much more efficient to reuse the space of temporary intermediate variables.
  ASSERT(IsClamped(a));
  ASSERT(IsClamped(b));
  ASSERT(!b.IsZero());

  int comp = UnsignedCompare(a, b);
  if (comp < 0) {
    (*quotient) ^= Zero();
    (*remainder) ^= Copy(a);  // TODO(floitsch): can we reuse the input?
    return;
  } else if (comp == 0) {
    (*quotient) ^= One();
    quotient->SetSign(a.IsNegative() != b.IsNegative());
    (*remainder) ^= Zero();
    return;
  }

  // High level description:
  // The algorithm is basically the algorithm that is taught in school:
  // Let a the dividend and b the divisor. We are looking for
  // the quotient q = truncate(a / b), and
  // the remainder r = a - q * b.
  // School algorithm:
  // q = 0
  // n = number_of_digits(a) - number_of_digits(b)
  // for (i = n; i >= 0; i--) {
  //   Maximize k such that k*y*10^i is less than or equal to a and
  //                  (k + 1)*y*10^i is greater.
  //   q = q + k * 10^i   // Add new digit to result.
  //   a = a - k * b * 10^i
  // }
  // r = a
  //
  // Instead of working in base 10 we work in base kDigitBitSize.

  intptr_t b_length = b.Length();
  int normalization_shift =
      kDigitBitSize - CountBits(b.GetChunkAt(b_length - 1));
  Bigint& dividend = Bigint::Handle(ShiftLeft(a, normalization_shift));
  const Bigint& divisor = Bigint::Handle(ShiftLeft(b, normalization_shift));
  dividend.SetSign(false);
  divisor.SetSign(false);

  intptr_t dividend_length = dividend.Length();
  intptr_t divisor_length = b_length;
  ASSERT(divisor_length == divisor.Length());

  intptr_t quotient_length = dividend_length - divisor_length + 1;
  *quotient ^= Bigint::Allocate(quotient_length);
  quotient->SetSign(a.IsNegative() != b.IsNegative());

  intptr_t quotient_pos = dividend_length - divisor_length;
  // Find the first quotient-digit.
  // The first digit must be computed separately from the other digits because
  // the preconditions for the loop are not yet satisfied.
  // For simplicity use a shifted divisor, so that the comparison and
  // subtraction are easier.
  int divisor_shift_amount = dividend_length - divisor_length;
  Bigint& shifted_divisor =
      Bigint::Handle(DigitsShiftLeft(divisor, divisor_shift_amount));
  Chunk first_quotient_digit = 0;
  while (UnsignedCompare(dividend, shifted_divisor) >= 0) {
    first_quotient_digit++;
    dividend ^= Subtract(dividend, shifted_divisor);
  }
  quotient->SetChunkAt(quotient_pos--, first_quotient_digit);

  // Find the remainder of the digits.

  Chunk first_divisor_digit = divisor.GetChunkAt(divisor_length - 1);
  // The short divisor only represents the first two digits of the divisor.
  // If the divisor has only one digit, then the second part is zeroed out.
  Bigint& short_divisor = Bigint::Handle(Bigint::Allocate(2));
  if (divisor_length > 1) {
    short_divisor.SetChunkAt(0, divisor.GetChunkAt(divisor_length - 2));
  } else {
    short_divisor.SetChunkAt(0, 0);
  }
  short_divisor.SetChunkAt(1, first_divisor_digit);
  // The following bigint will be used inside the loop. It is allocated outside
  // the loop to avoid repeated allocations.
  Bigint& target = Bigint::Handle(Bigint::Allocate(3));
  // The dividend_length here must be from the initial dividend.
  for (intptr_t i = dividend_length - 1; i >= divisor_length; i--) {
    // Invariant: let t = i - divisor_length
    //   then dividend / (divisor << (t * kDigitBitSize)) <= kDigitMaxValue.
    // Ex: dividend: 53451232, and divisor: 535  (with t == 5) is ok.
    //     dividend: 56822123, and divisor: 563  (with t == 5) is bad.
    //     dividend:  6822123, and divisor: 563  (with t == 5) is ok.

    // The dividend has changed. So recompute its length.
    dividend_length = dividend.Length();
    Chunk dividend_digit;
    if (i > dividend_length) {
      quotient->SetChunkAt(quotient_pos--, 0);
      continue;
    } else if (i == dividend_length) {
      dividend_digit = 0;
    } else {
      ASSERT(i + 1 == dividend_length);
      dividend_digit = dividend.GetChunkAt(i);
    }
    Chunk quotient_digit;
    // Compute an estimate of the quotient_digit. The estimate will never
    // be too small.
    if (dividend_digit == first_divisor_digit) {
      // Small shortcut: the else-branch would compute a value > kDigitMaxValue.
      // However, by hypothesis, we know that the quotient_digit must fit into
      // a digit. Avoid going through repeated iterations of the adjustment
      // loop by directly assigning kDigitMaxValue to the quotient_digit.
      // Ex:  51235 / 523.
      //    51 / 5 would yield 10 (if computed in the else branch).
      // However we know that 9 is the maximal value.
      quotient_digit = kDigitMaxValue;
    } else {
      // Compute the estimate by using two digits of the dividend and one of
      // the divisor.
      // Ex: 32421 / 535
      //    32 / 5 -> 6
      // The estimate would hence be 6.
      DoubleChunk two_dividend_digits = dividend_digit;
      two_dividend_digits <<= kDigitBitSize;
      two_dividend_digits += dividend.GetChunkAt(i - 1);
      DoubleChunk q = two_dividend_digits / first_divisor_digit;
      if (q > kDigitMaxValue) q = kDigitMaxValue;
      quotient_digit = static_cast<Chunk>(q);
    }

    // Refine estimation.
    quotient_digit++;  // The following loop will start by decrementing.
    Bigint& estimation_product = Bigint::Handle();
    target.SetChunkAt(0, ((i - 2) < 0) ? 0 : dividend.GetChunkAt(i - 2));
    target.SetChunkAt(1, ((i - 1) < 0) ? 0 : dividend.GetChunkAt(i - 1));
    target.SetChunkAt(2, dividend_digit);
    do {
      quotient_digit = (quotient_digit - 1) & kDigitMask;
      estimation_product ^= MultiplyWithDigit(short_divisor, quotient_digit);
    } while (UnsignedCompareNonClamped(estimation_product, target) > 0);
    // At this point the quotient_digit is fairly accurate.
    // At the worst it is off by one.
    // Remove a multiple of the divisor. If the estimate is incorrect we will
    // subtract the divisor another time.
    // Let t = i - divisor_length.
    // dividend -= (quotient_digit * divisor) << (t * kDigitBitSize);
    shifted_divisor ^= MultiplyWithDigit(divisor, quotient_digit);
    shifted_divisor ^= DigitsShiftLeft(shifted_divisor, i - divisor_length);
    dividend = Subtract(dividend, shifted_divisor);
    if (dividend.IsNegative()) {
      // The estimation was still too big.
      quotient_digit--;
      // TODO(floitsch): allocate space for the shifted_divisor once and reuse
      // it at every iteration.
      shifted_divisor ^= DigitsShiftLeft(divisor, i - divisor_length);
      // TODO(floitsch): reuse the space of the previous dividend.
      dividend = Add(dividend, shifted_divisor);
    }
    quotient->SetChunkAt(quotient_pos--, quotient_digit);
  }
  ASSERT(quotient_pos == -1);
  Clamp(*quotient);
  *remainder ^= ShiftRight(dividend, normalization_shift);
  remainder->SetSign(a.IsNegative());
}


void BigintOperations::Clamp(const Bigint& bigint) {
  intptr_t length = bigint.Length();
  while (length > 0 && (bigint.GetChunkAt(length - 1) == 0)) {
    length--;
  }
  // TODO(floitsch): We change the size of bigint-objects here.
  bigint.SetLength(length);
}


RawBigint* BigintOperations::Copy(const Bigint& bigint) {
  intptr_t bigint_length = bigint.Length();
  Bigint& copy = Bigint::Handle(Bigint::Allocate(bigint_length));
  for (intptr_t i = 0; i < bigint_length; i++) {
    copy.SetChunkAt(i, bigint.GetChunkAt(i));
  }
  copy.SetSign(bigint.IsNegative());
  return copy.raw();
}


int BigintOperations::CountBits(Chunk digit) {
  int result = 0;
  while (digit != 0) {
    digit >>= 1;
    result++;
  }
  return result;
}

}  // namespace dart
