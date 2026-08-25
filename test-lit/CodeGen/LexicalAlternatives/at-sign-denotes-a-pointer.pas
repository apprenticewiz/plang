(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42 42 7
*)

program p(output);
type iptr = @integer;
var q: iptr; r: ^integer;
begin
  new(q); q@ := 42;
  new(r); r^ := 7;
  writeln(q@:1, ' ', q^:1, ' ', r@:1)
end.
