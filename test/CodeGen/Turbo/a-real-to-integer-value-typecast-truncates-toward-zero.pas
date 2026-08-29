(*
Integer(SomeReal) is a VALUE typecast between two ordinal-or-real types with
different sizes, so Sema::checkTypeCast accepts it as a numeric conversion
and CodeGen's emitTypeCastValue lowers it through the same generic
coerceToType helper every other real-to-integer coercion in this compiler
already uses -- FPToSI, which truncates toward zero, exactly like Trunc,
and NOT like Round (which would turn 3.7 into 4 and -3.7 into -4).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:-3
CHECK-NEXT:3
*)

program p;
var
  r: Real;
  i: Integer;
begin
  r := 3.7;
  i := Integer(r);
  writeln(i);

  r := -3.7;
  i := Integer(r);
  writeln(i);

  r := 3.2;
  i := Integer(r);
  writeln(i);
end.
