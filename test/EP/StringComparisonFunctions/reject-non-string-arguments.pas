(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: cannot be an argument of eq
*)

program p(output);
begin writeln(eq(3, 5)) end.
