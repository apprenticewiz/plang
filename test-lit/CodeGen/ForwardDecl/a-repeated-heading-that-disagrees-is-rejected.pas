(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not match forward declaration
*)

program p;
procedure b(n: integer); forward;
procedure b(n: real);
begin writeln(n:0:1) end;
begin b(1.0) end.
