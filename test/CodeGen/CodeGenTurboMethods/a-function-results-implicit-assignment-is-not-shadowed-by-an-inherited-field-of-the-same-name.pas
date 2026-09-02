(*
Issue #626: TD's own function 'Count' shares its name with TA's own
INHERITED field 'Count' -- pushMethodSelfScope exposes every field (own or
ancestor-inherited; RecordFields is already the flattened list) unqualified
inside the method body, and a bare 'Count := 999;' assignment-target lookup
used to find that FIELD symbol first, both misdirecting the write (it set
the field, not the function's own result) and then failing the "function
does not assign to its result variable" audit outright, since -- as far as
Sema could tell -- nothing had.

Issue #781 corrected this test's own outside-the-method expectation: a bare,
qualified read of the same name ('D.Count', no parens) ALSO calls the
METHOD, not the field -- confirmed against a local `fpc -Mtp` build, which
prints 999 for both 'D.Count()' and bare 'D.Count' (even 'Self.Count' from
inside another method of TD calls the method, not the field; a same-named
method entirely shadows an inherited field for every unqualified spelling,
with no bare syntax left to reach the field once shadowed).
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TA = object
    Count: Integer;
  end;
  TD = object(TA)
    function Count: Integer;
  end;

function TD.Count: Integer;
begin
  Count := 999;
end;

var
  D: TD;
begin
  writeln(D.Count());
  writeln(D.Count);
end.

(*
CHECK:999
CHECK-NEXT:999
*)
