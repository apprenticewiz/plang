(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:neg
CHECK-NEXT:zero
CHECK-NEXT:pos
CHECK-NEXT:-1 42
*)

(*
ISO 7185 Sec6.3: constant = [ sign ] ( unsigned-number | constant-identifier )
               | character-string.  A case-constant (Sec6.8.3.5) is a
constant, and so is a variant-part's case-constant (Sec6.4.3.3) -- both are
the very same grammar production, so both must accept a leading sign.

parseCaseLabel (ParseStmt.cpp) and parseVariantPart's label loop
(ParseType.cpp) both parsed a label with a bare parseFactor() call, which
has no sign production of its own -- only parseSimpleExpr, one level up,
does -- so `-1: writeln('neg')` failed with "expected expression, got '-'"
in both an ordinary case-statement and a variant-part.  parseSubrangeBound
already carries this same sign-handling for a subrange bound's own
constant; case-constants need the same treatment.
*)

program p(output);
type
  r = record
        case t: integer of
          -1: (neg: integer);
           0: (zero: integer);
           1: (pos: integer)
      end;
var i: integer; v: r;
begin
  for i := -1 to 1 do
    case i of
      -1: writeln('neg');
       0: writeln('zero');
       1: writeln('pos')
    end;
  v.t := -1; v.neg := 42;
  writeln(v.t:1, ' ', v.neg:1)
end.
