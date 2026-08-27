(*
RUN: not %plang -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program t;
procedure p;
begin
end;
begin
  for p := 1 to 3 do writeln(1)
end.

(*
CHECK: is not a variable
*)
