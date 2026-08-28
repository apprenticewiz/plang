(*
Continue's mirror of break-inside-a-nested-procedure-does-not-inherit-the-
callers-loop.pas -- see that file's own comment.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'continue' is not valid outside a while, for, for-in or repeat loop
*)

program p;
procedure Outer;
  procedure Inner;
  begin
    Continue
  end;
var i: Integer;
begin
  i := 0;
  while i < 5 do begin
    i := i + 1;
    Inner
  end
end;
begin
  Outer
end.
