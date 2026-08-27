(*
Issue #144.  children()/_layout_walk()/_eval_form() in
plang_schema_printers.py used to have zero exception handling, so an
unexpected "kind" on one field (neither "array" nor "scalar" -- a sidecar
this script has no business trusting blindly) threw an unhandled Python
exception that gdb spliced into the print output as a raw traceback,
silently dropping every field after the failure point.

This is a per-FIELD problem, not a structural one -- the rest of the
schema entry is still well-formed -- so _layout_walk now catches it and
folds it into an error-marker child value for just that field, rather
than losing the fields before it too.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: python3 -c "import json; p='%s.plang-schemas.json'; d=json.load(open(p)); d['schemas']['Rec'][0]['fields'][1]['kind']='bogus'; json.dump(d, open(p, 'w'))"
RUN: gdb -q -batch -ex "source %plang_schema_printers" -ex "break %s:35" -ex run -ex "print q^" %t 2>&1 | FileCheck %s
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
CHECK: Rec = [[SEP:.*]]n = 3, a = [[SEP2:.*]]11, 22, 33[[SEP3:.*]]k = <plang-schema-error: unknown field kind 'bogus'>
CHECK-NOT: Traceback
CHECK-NOT: Python Exception
*)
