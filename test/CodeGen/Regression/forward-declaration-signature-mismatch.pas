(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: does not match forward declaration
*)

program p;
procedure foo(x: integer); forward;
procedure foo(x: real);
begin writeln(x) end;
begin foo(1) end.
