(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: only for a record with a variant part
*)

program p; var q: ^integer;
begin new(q, 8) end.
