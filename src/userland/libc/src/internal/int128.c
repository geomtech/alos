/* Support de division entiere large du compilateur, sans runtime hote. */
typedef unsigned __int128 wide_t;
typedef __int128 signed_wide_t;

wide_t __udivmodti4(wide_t value, wide_t divisor, wide_t *remainder) {
  if (!divisor) __builtin_trap();
  wide_t quotient = 0, rest = 0;
  for (int bit = 127; bit >= 0; --bit) {
    unsigned carry = (unsigned)(rest >> 127);
    rest = (rest << 1) | ((value >> bit) & 1);
    if (carry || rest >= divisor) {
      rest -= divisor;
      quotient |= (wide_t)1 << bit;
    }
  }
  if (remainder) *remainder = rest;
  return quotient;
}

wide_t __udivti3(wide_t value, wide_t divisor) {
  return __udivmodti4(value, divisor, 0);
}

wide_t __umodti3(wide_t value, wide_t divisor) {
  wide_t remainder;
  __udivmodti4(value, divisor, &remainder);
  return remainder;
}

signed_wide_t __divti3(signed_wide_t value, signed_wide_t divisor) {
  int negative = (value < 0) ^ (divisor < 0);
  wide_t magnitude = value < 0 ? (wide_t)0 - (wide_t)value : (wide_t)value;
  wide_t divisor_magnitude = divisor < 0 ? (wide_t)0 - (wide_t)divisor : (wide_t)divisor;
  wide_t quotient = __udivti3(magnitude, divisor_magnitude);
  return negative ? -(signed_wide_t)quotient : (signed_wide_t)quotient;
}

signed_wide_t __modti3(signed_wide_t value, signed_wide_t divisor) {
  wide_t magnitude = value < 0 ? (wide_t)0 - (wide_t)value : (wide_t)value;
  wide_t divisor_magnitude = divisor < 0 ? (wide_t)0 - (wide_t)divisor : (wide_t)divisor;
  wide_t remainder = __umodti3(magnitude, divisor_magnitude);
  return value < 0 ? -(signed_wide_t)remainder : (signed_wide_t)remainder;
}
