(*
A Pascal nested procedure's declaration can only appear in its enclosing
block's DECLARATION part, never inside a loop's STATEMENT body -- so a
procedure declared inside Outer and called from a loop in Outer's own body
has, from ITS OWN perspective, no enclosing loop at all: LoopDepth_ is reset
to 0 for its body (checkProcBody, Sema.h's own comment), independently of
however many loops its CALLER happens to be nested in at the call site.
Confirmed to match `fpc -Mtp`'s own "BREAK not allowed" for the identical
program (empirically).
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'break' is not valid outside a while, for, for-in or repeat loop
*)

program p;
procedure Outer;
  procedure Inner;
  begin
    Break
  end;
var i: Integer;
begin
  for i := 1 to 5 do Inner
end;
begin
  Outer
end.
