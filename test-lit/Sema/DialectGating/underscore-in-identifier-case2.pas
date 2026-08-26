(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
var my_var: integer;
begin my_var := 1 end.

(*
CHECK: an underscore in an identifier is an Extended Pascal extension
*)
