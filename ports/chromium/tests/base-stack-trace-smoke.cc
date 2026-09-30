#include "base/debug/stack_trace.h"

#include <stdio.h>
#include <string>

int main() {
  using base::debug::StackTrace;
  const void* capture[2] = {nullptr, nullptr};
  if (base::debug::CollectStackTrace({}) != 0 ||
      base::debug::CollectStackTrace(capture) != 1 ||
      !capture[0] || capture[1] ||
      StackTrace(0).addresses().size() != 0 ||
      !StackTrace(0).ToString().empty() ||
      StackTrace().addresses().size() != 1 ||
      StackTrace::WillSymbolizeToStreamForTesting()) {
    puts("chromium-stack-trace-smoke: FAIL single-PC capture");
    return 1;
  }
  const void* supplied[] = {
      reinterpret_cast<const void*>(0x1234),
      reinterpret_cast<const void*>(0xabcdef)};
  StackTrace trace(supplied);
  if (trace.addresses().size() != 2 ||
      trace.ToString() != "#0 0x1234\n#1 0xabcdef\n" ||
      trace.ToStringWithPrefix("raw: ") !=
          "raw: #0 0x1234\nraw: #1 0xabcdef\n" ||
      base::debug::EnableInProcessStackDumping()) {
    puts("chromium-stack-trace-smoke: FAIL raw output/capability");
    return 1;
  }
  trace.PrintWithPrefix("raw: ");
  puts("chromium-stack-trace-smoke: PASS (one PC, supplied raw addresses)");
  return 0;
}
