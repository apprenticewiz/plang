(*
FillChar(var X; Count: Integer; Value) -- X is "untyped" the way real Turbo
Pascal's is: any variable at all, addressed directly and filled
byte-for-byte with Value's own low byte for the first Count bytes, its own
declared type not otherwise examined.  Lowered to llvm.memset
(CGProcCall::emitCallStmt).  Exercised with both a char Value and an
ordinal (Byte) Value, and with Count less than the variable's own full
size, to show only the first Count bytes are touched.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-----fghij
CHECK-NEXT:00000fghij
*)

program p;
var
  buf: array[1 .. 10] of Char;
  k: Integer;
begin
  for k := 1 to 10 do buf[k] := Chr(Ord('a') + k - 1);
  FillChar(buf, 5, '-');
  for k := 1 to 10 do write(buf[k]);
  writeln;

  for k := 1 to 10 do buf[k] := Chr(Ord('a') + k - 1);
  FillChar(buf, 5, 48); { ordinal Value: 48 = Ord('0') }
  for k := 1 to 10 do write(buf[k]);
  writeln;
end.
