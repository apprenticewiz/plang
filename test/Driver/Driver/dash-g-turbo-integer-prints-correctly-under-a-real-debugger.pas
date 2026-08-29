(*
Companion to dash-g-gives-turbos-16-bit-integer-its-real-width-not-a-
hardcoded-64.pas: that test checks the DWARF metadata shape in the emitted
IR directly, but this project has already been burned once by a class of
debug-info bug an IR-text check alone did not catch -- see
lib/CodeGen/CGDebugInfo.h's own notes and the project history behind this
fix.  This test runs a real gdb session instead.

Before the fix this guards, CGDebugInfo::debugTypeOfSemaType's Integer case
told DWARF every Integer was an 8-byte signed value no matter what -- true
for ISO 7185/Extended Pascal, but Turbo's Integer is a real 16-bit signed
type (LangOptions::defaultIntWidth()) and its LLVM storage really is only 2
bytes.  gdb, trusting the (wrong) 8-byte DW_AT_byte_size, read `i`'s real 2
bytes plus 6 bytes of whatever followed it in memory and printed 64302
instead of -1234 -- confirmed manually against this exact program before
the fix landed.  After the fix, sizeof(i) reports 2 and the printed value
is correct.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -std=turbo -g %s -o %t
RUN: gdb -q -batch -ex "break %s:33" -ex run -ex "print sizeof(i)" -ex "print i" %t 2>&1 | FileCheck %s
*)

program p;
var i: Integer;
begin
  i := -1234;
  writeln(i)
end.

(*
CHECK: $1 = 2
CHECK: $2 = -1234
*)
