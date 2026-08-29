(*
Sema::byteSizeOf's own doc comment (Sema.h) gives the motivating example
verbatim: "Turbo writes `const BufSize = 4 * SizeOf(Integer)` and a
constant has to fold" -- SizeOf(T) has to be usable in a constant-expression
context (a const declaration's initializer, an array bound, ...), not only
as an ordinary runtime call.  Sema::constBoundImpl's own BuiltinID::SizeOf/
High/Low arm (SemaType.cpp) is what makes that work, reading back
Args[0]->ResolvedType that checkCallExpr's SizeOf/High/Low arm already left
behind rather than re-deriving "is this a type name or a value" a second
time.

High/Low fold too, and are exercised here driving an array's own declared
bound.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8
CHECK-NEXT:2
CHECK-NEXT:99
*)

program p;
const
  BufSize = 4 * SizeOf(Integer);
type
  TSmall = 0 .. 2;
var
  buf: array[1 .. BufSize] of Byte;
  arr: array[Low(TSmall) .. High(TSmall)] of Integer;
begin
  writeln(BufSize);
  writeln(High(arr));
  arr[High(arr)] := 99;
  writeln(arr[2]);
  buf[1] := 0; { silence unused-variable concerns for buf }
end.
