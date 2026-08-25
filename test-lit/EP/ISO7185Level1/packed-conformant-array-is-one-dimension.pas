(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: one dimension
*)

program p;
procedure q(s: packed array [lo..hi: integer;
                             j..k: integer] of char);
begin end;
begin end.
