(*
-Werror promotes a warning to an error, which rejects the program right
at compile time.
*)

(*
RUN: not %plang %s -Werror -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: error
*)

program p(output);
var i: integer;
begin writeln(i) end.
