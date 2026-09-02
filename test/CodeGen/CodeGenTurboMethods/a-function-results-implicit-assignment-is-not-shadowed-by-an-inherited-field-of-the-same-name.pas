(*
Issue #626: TD's own function 'Count' shares its name with TA's own
INHERITED field 'Count' -- pushMethodSelfScope exposes every field (own or
ancestor-inherited; RecordFields is already the flattened list) unqualified
inside the method body, and a bare 'Count := 999;' assignment-target lookup
used to find that FIELD symbol first, both misdirecting the write (it set
the field, not the function's own result) and then failing the "function
does not assign to its result variable" audit outright, since -- as far as
Sema could tell -- nothing had.  Confirmed against a local `fpc -Mtp`
build: the function-result binding wins a bare assignment-target of the
same name as the method itself, even with an inherited field of that exact
name in scope; the field itself is untouched (still its zero-initialized
default), reachable only through an explicit 'Self.Count'.
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
CHECK-NEXT:0
*)
