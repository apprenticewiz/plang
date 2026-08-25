(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not assign to its result
*)

program p(output);
function f: integer;
begin end;
begin writeln(f) end.
