(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: type mismatch in write
*)

program p;
var f: file of integer;
begin
  rewrite(f);
  write(f, 1.5)
end.
