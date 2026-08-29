(*
Inc(x[, n]) / Dec(x[, n]) mutate x in place by n, defaulting to 1 -- EP
§6.7.6.5's succ/pred two-argument form by another name, except succ/pred
READ a new value and Inc/Dec WRITE one back into x.  CGProcCall's own
Inc/Dec lowering mirrors CGFuncCall's succ/pred lowering exactly (load,
widen, add/sub, range-check, narrow back) and then stores the result into
x instead of returning it.

Exercised over a plain Integer, a Byte (the sized-integer ladder), and an
enumeration (Inc/Dec stay in the argument's own ordinal type, same as
succ/pred).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:10
CHECK-NEXT:20
CHECK-NEXT:10
CHECK-NEXT:250
CHECK-NEXT:245
CHECK-NEXT:1
CHECK-NEXT:0
*)

program p;
type
  TColor = (Red, Green, Blue);
var
  i: Integer;
  b: Byte;
  c: TColor;
begin
  i := 10;
  Inc(i);       writeln(i);
  Dec(i);       writeln(i);
  Inc(i, 10);   writeln(i);
  Dec(i, 10);   writeln(i);

  b := 245;
  Inc(b, 5);    writeln(b);
  Dec(b, 5);    writeln(b);

  c := Red;
  Inc(c);       writeln(Ord(c));
  Dec(c);       writeln(Ord(c));
end.
