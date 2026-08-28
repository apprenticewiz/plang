(*
The case most likely to reveal a wrong target: a Break/Continue inside a
loop nested inside another loop must affect only the INNERMOST one --
CGFunction::LoopStack (CodeGenImpl.h) is a per-activation STACK precisely so
that CGProcCall's own Break/Continue arm always reads .back(), the
innermost pair CGControlFlow pushed.  The outer `for` runs 3 times
regardless of what the inner loop's Break/Continue do; if Break's target
were wrong (e.g. accidentally the OUTER loop's exit block) the outer loop
would run fewer than 3 times or the "after inner loop" lines would go
missing.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t > %t.out
RUN: FileCheck %s < %t.out
*)

(*
CHECK: outer i=1
CHECK-NEXT:   inner j=1
CHECK-NEXT:   inner j=3
CHECK-NEXT: outer i=1 after inner loop
CHECK-NEXT: outer i=2
CHECK-NEXT:   inner j=1
CHECK-NEXT:   inner j=3
CHECK-NEXT: outer i=2 after inner loop
CHECK-NEXT: outer i=3
CHECK-NEXT:   inner j=1
CHECK-NEXT:   inner j=3
CHECK-NEXT: outer i=3 after inner loop
CHECK-NEXT: done
*)

program nested_loops;
var i, j: Integer;
begin
  for i := 1 to 3 do begin
    writeln('outer i=', i);
    for j := 1 to 5 do begin
      if j = 4 then Break;
      if (j mod 2) = 0 then Continue;
      writeln('  inner j=', j)
    end;
    writeln('outer i=', i, ' after inner loop')
  end;
  writeln('done')
end.
