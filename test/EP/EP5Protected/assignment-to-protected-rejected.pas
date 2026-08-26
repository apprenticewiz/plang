(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: protected
*)

program p;
procedure q(protected x: integer);
begin x := 5 end;
begin q(1) end.
