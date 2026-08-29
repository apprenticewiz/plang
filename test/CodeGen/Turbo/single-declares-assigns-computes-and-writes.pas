(*
Turbo's Single (32-bit float, alongside the dialects' own 64-bit Real) end
to end: declare it, assign a literal and an Integer (the implicit widening
isAssignCompatible already grants Integer -> any Real-kind type), do
arithmetic mixing it with both another Single and a plain Real, negate one,
and write every result out.  Real (64-bit) stays completely unaffected --
r's own arithmetic and output are the same as before Single existed.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.7500
CHECK-NEXT:3.750000
CHECK-NEXT:5.00
CHECK-NEXT:-5.00
CHECK-NEXT:5.00
CHECK-NEXT:25.00
*)

var
  s1, s2, s3: Single;
  r: Real;
  i: Integer;
begin
  s1 := 1.5;
  s2 := 2.25;
  s3 := s1 + s2;             // Single + Single
  writeln(s3:0:4);
  r := s1 + s2;               // Single + Single -> widens into a Real dest
  writeln(r:0:6);
  i := 5;
  s1 := i;                    // Integer -> Single
  writeln(s1:0:2);
  s2 := -s1;                  // unary minus on a Single
  writeln(s2:0:2);
  writeln(abs(s2):0:2);
  writeln(sqr(s1):0:2);
end.
