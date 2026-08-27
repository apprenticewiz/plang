(*
Issue #144.  _eval_form in plang_schema_printers.py used to recurse over a
field's ExtentForm with no depth guard at all, so a pathological (corrupt
or hand-edited) sidecar nesting one past Python's own default recursion
limit crashed with a raw RecursionError spliced into the print output
instead of a clean, bounded error.

_eval_form now carries a recursion-depth guard matching Sema's own
MaxExprDepth (an ExtentForm's nesting is bounded by the same
expression-depth guard on the compiler side, so a form nested past that is
never a real one) and raises a plain, catchable error instead.
*)

(*
REQUIRES: gdb-binary
*)

(*
RUN: %plang -g -std=iso10206 %s -o %t
RUN: python3 -c "import json, sys; from functools import reduce; sys.setrecursionlimit(6000); p='%s.plang-schemas.json'; d=json.load(open(p)); d['schemas']['Rec']['fields'][0]['high']=reduce(lambda a,_:['add',a,['const',0]],range(1500),['const',1]); json.dump(d, open(p, 'w'))"
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
CHECK: Rec = [[SEP:.*]]n = 3, a = <plang-schema-error: extent form nested past 1000 levels>
CHECK-NOT: Traceback
CHECK-NOT: RecursionError
*)
