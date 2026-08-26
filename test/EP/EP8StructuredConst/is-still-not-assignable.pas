(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: not an assignable variable
*)

program p;
type row = array[1..3] of integer;
const v = row[otherwise 1];
begin v[1] := 9 end.
