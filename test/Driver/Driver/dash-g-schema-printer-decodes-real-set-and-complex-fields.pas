(*
Issue #144.  plang_schema_printers.py used to read every non-array field as
a raw signed 64-bit integer no matter its real plang type: a real field
showed its IEEE-754 bit pattern instead of its actual value, a set field
showed an arbitrary integer instead of its member list, and a 16-byte
field (complex) crashed the printer outright with a Python OverflowError
(gdb's own Python-to-Value conversion cannot hold a 128-bit integer).

CGDebugInfo::recordSchemaLayoutForScript now tags each scalar field with
its real plang type ("typeKind": "real"/"complex"/"set"/... -- see
CGDebugInfo.cpp), and plang_schema_printers.py formats each field
according to that tag instead of blindly reading raw bytes as a signed
integer.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:40" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
*)

program p;
type Rec(n: integer) = record
  a: array[1..n] of integer;
  x: real;
  c: complex;
  s: set of 1..10
end;
type RecPtr = ^Rec;
var q: RecPtr;
begin
  new(q, 3);
  q^.a[1] := 11;
  q^.x := 3.5;
  q^.c := cmplx(1.0, 2.0);
  q^.s := [1, 3, 5];
  writeln(q^.a[1])
end.

(*
CHECK: Rec = [[SEP1:.*]]n = 3, a = [[SEP2:.*]]11, 0, 0[[SEP3:.*]]x = 3.5, c = [[SEP4:.*]]re = 1.0, im = 2.0[[SEP5:.*]]s = [1, 3, 5]
*)
