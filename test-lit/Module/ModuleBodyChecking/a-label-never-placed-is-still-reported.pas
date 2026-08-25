(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: never defined as a labeled statement
*)

module M;
  label 1;
  procedure q; begin writeln('q') end;
end.
program p;
  import M;
begin q end.
