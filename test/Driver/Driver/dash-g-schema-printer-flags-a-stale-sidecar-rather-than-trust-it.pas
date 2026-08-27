(*
Issue #141.  The sidecar was written to <source>.plang-schemas.json with no
content hash, binary identifier or timestamp tying a specific BUILT BINARY
to "its" sidecar.  A rebuild of the same source (to any output name, even
one that never asked for -g) silently overwrote the one sidecar file next
to it, so an OLDER already-built -g binary's debug session could load a
sidecar describing a DIFFERENT compile and confidently print wrong values.

This compiles the same source TWICE, to two different output paths -- both
write to the identical sidecar (it is named after the SOURCE path, not the
output path), so the second compile's own random per-compile "buildId"
overwrites the first's.  Debugging the FIRST (older) binary against that
now-mismatched sidecar must not silently trust it: the pretty-printer
should warn once to stderr and fall back to gdb's own default (DWARF-only,
honestly approximate) printing -- which places 'k' (declared after the
varying array 'a') at the WRONG offset, so it prints something other than
777, not a crash and not a confidently-wrong 777 borrowed from a stale
sidecar that no longer describes this exact binary.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: %plang -g -std=iso10206 %s -o %t.rebuilt
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:41" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
*)

program p;
type Rec(n: integer) = record a: array[1..n] of integer; k: integer end;
type RecPtr = ^Rec;
var q: RecPtr;
begin
  new(q, 3);
  q^.a[1] := 11;
  q^.a[2] := 22;
  q^.a[3] := 33;
  q^.k := 777;
  writeln(q^.a[3], ' ', q^.k)
end.

(*
CHECK: plang schema pretty-printer: sidecar buildId [[SID:[0-9a-f]+]] does not match this binary's own [[LID:[0-9a-f]+]]
CHECK-NOT: n = 3, a = [[SEP:.*]]11, 22, 33[[SEP2:.*]]k = 777
*)
