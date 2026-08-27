(*
Issue #144.  children()/_layout_walk()/_eval_form() in
plang_schema_printers.py used to have zero exception handling, so a
sidecar missing an expected key threw an unhandled Python exception that
gdb spliced into the print output as a raw traceback, silently dropping
every field after the failure point.

A schema entry missing "fields" entirely has no layout to walk at all --
there is no single field to blame, so the top-level lookup function now
returns None for it, and gdb falls back to its own default DWARF-only
printing instead of a broken pretty-printer or a raw traceback.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: python3 -c "import json; p='%s.plang-schemas.json'; d=json.load(open(p)); del d['schemas']['Rec']['fields']; json.dump(d, open(p, 'w'))"
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:34" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
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
CHECK: $1 = [[SEP:.*]]n = 3
CHECK-NOT: Traceback
CHECK-NOT: Python Exception
*)
