(*
TP-only Break/Continue (Builtins.def, reached through CallStmt like Halt/
Exit) inside each of the three loop kinds `for ... in` does not need
(while/for/repeat -- for-in is EP-only in this compiler and Break/Continue
are TP-only, so the two can never appear in the same program; verified
separately by code inspection, see CGControlFlow.cpp's own comments).  Each
loop counts 1..10, breaking at 5 and skipping even numbers, so which
iterations actually printed is the observable proof of both the break and
the continue target: printing only 1 and 3 shows continue skipped 2 and 4
without also skipping past 5 (which would prove the wrong target -- a
continue landing on the loop's OWN condition/until test re-evaluates it
without re-entering an already-passed iteration, while a continue landing
elsewhere could easily loop forever or terminate early instead).
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out
RUN: FileCheck %s < %t.out
*)

(*
CHECK: while odd: 1
CHECK-NEXT: while odd: 3
CHECK-NEXT: while final i=5
CHECK-NEXT: repeat odd: 1
CHECK-NEXT: repeat odd: 3
CHECK-NEXT: repeat final i=5
CHECK-NEXT: for odd: 1
CHECK-NEXT: for odd: 3
CHECK-NEXT: for final i=5
*)

program break_continue_loops;
var i: Integer;
begin
  i := 0;
  while i < 10 do begin
    i := i + 1;
    if i = 5 then Break;
    if (i mod 2) = 0 then Continue;
    writeln('while odd: ', i)
  end;
  writeln('while final i=', i);

  i := 0;
  repeat
    i := i + 1;
    if i = 5 then Break;
    if (i mod 2) = 0 then Continue;
    writeln('repeat odd: ', i)
  until i >= 10;
  writeln('repeat final i=', i);

  for i := 1 to 10 do begin
    if i = 5 then Break;
    if (i mod 2) = 0 then Continue;
    writeln('for odd: ', i)
  end;
  writeln('for final i=', i)
end.
