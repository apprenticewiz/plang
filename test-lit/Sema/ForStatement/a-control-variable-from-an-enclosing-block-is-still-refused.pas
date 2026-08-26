(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
var i: integer;
procedure q; begin for i := 1 to 3 do end;
begin q end.

(*
CHECK: must be declared in the immediately enclosing block
*)
