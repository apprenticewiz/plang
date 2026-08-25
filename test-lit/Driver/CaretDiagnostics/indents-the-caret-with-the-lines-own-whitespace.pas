(*
The caret line's own leading whitespace matches the source line's, so an
indented statement still gets a caret directly under the column named --
not just under some fixed offset. A single CHECK/CHECK-NEXT pair proves
this (the original assertion's embedded newline can't be one FileCheck
pattern -- FileCheck directives are inherently line-based).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.err
*)

(*
CHECK:        x := notdeclared
CHECK-NEXT:             ^
*)

program p;
var x: integer;
begin
        x := notdeclared
end.
