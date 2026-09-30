/* Support de division entiere large du compilateur, sans runtime hote. */
typedef unsigned __int128 wide_t;

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
