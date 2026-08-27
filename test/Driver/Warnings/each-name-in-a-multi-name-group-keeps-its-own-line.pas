(*
A var-group can name several identifiers before the type that covers all
of them ("first, second, third: integer"). The "declared but never used"
warning for each one has to point at that identifier's own token, not at
the shared type that ends the group -- otherwise every warning quotes the
same source line ("third: integer;") and the same caret column, no matter
which name it is actually about (issue #128). Verified here the way
Driver/CaretDiagnostics does: pin the quoted source line and the caret's
exact column for each name separately, rather than the line number, which
would shift with this header comment's own length.

RUN: %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=FIRST --strict-whitespace --match-full-lines %s < %t.err
RUN: FileCheck --check-prefix=SECOND --strict-whitespace --match-full-lines %s < %t.err
RUN: FileCheck --check-prefix=THIRD --strict-whitespace --match-full-lines %s < %t.err
*)

(*
FIRST:  first,
FIRST-NEXT:  ^
*)
(*
SECOND:  second,
SECOND-NEXT:  ^
*)
(*
THIRD:  third: integer;
THIRD-NEXT:  ^
*)

program p;
var
  first,
  second,
  third: integer;
begin
end.
