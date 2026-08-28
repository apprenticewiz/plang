(*
Turbo's '@' is a prefix address-of operator: `p := @x` takes the address of
a variable and hands back a typed pointer, a different thing from '^'
(postfix dereference, or a pointer type's prefix marker) -- see
test/CodeGen/LexicalAlternatives/at-sign-denotes-a-pointer.pas, where under
the default (ISO 7185) dialect '@' is instead just another spelling of '^'
and this same source would mean something else entirely.

Exercises the address-of path (Sema::checkUnary's At case, lowered through
CGBinaryOps::emitUnary to the same EmitLValue every 'var'-parameter/
assignment-target already reuses) against a plain variable, a record field
and an array element, then reads every one of them back through '^' to
prove the address round-trips to the value that was actually there.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
CHECK-NEXT:10 20
CHECK-NEXT:99
*)

program p;
type rec = record a, b: Integer end;
var
  x: Integer;
  p: ^Integer;
  r: rec;
  pa, pb: ^Integer;
  arr: array[1..3] of Integer;
begin
  x := 5;
  p := @x;
  writeln(p^);

  r.a := 10;
  r.b := 20;
  pa := @r.a;
  pb := @r.b;
  writeln(pa^, ' ', pb^);

  arr[2] := 99;
  pa := @arr[2];
  writeln(pa^)
end.
