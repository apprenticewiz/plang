(*
Continue (Builtins.def) skips to the next iteration of the loop it is
written in, so -- the same as Break's own test right next to this one --
nothing in the SAME statement sequence after it ever runs.
*)

(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: this statement cannot be reached
CHECK-NEXT: writeln('dead-inside-loop-body')
CHECK-NOT: cannot be reached
*)

program p;
var i: Integer;
begin
  for i := 1 to 10 do
  begin
    continue;
    writeln('dead-inside-loop-body')
  end;
  writeln('reachable-after-loop')
end.
