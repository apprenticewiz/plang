(*
An IFDEF/ELSEIF/ELSE chain behaves like Pascal's own if/else if/else:
exactly one branch runs, whichever is the first whose own symbol is
defined, and every ELSEIF after that point is never evaluated at all --
not even to decide it is false. Three split-file variants prove this:
the first branch (A) satisfied, skipping two later branches (one of which,
C, would itself be a compile error if it were ever reached, since C is not
a valid Pascal identifier -- catching a naive implementation that
evaluates every ELSEIF instead of stopping at the first satisfied one);
the middle branch (B) satisfied, skipping A above it and C below it; and
no branch satisfied, falling through to ELSE.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/a-wins.pas -o %t.dir/a.bin
RUN: %run %t.dir/a.bin | FileCheck --check-prefix=A --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/b-wins.pas -o %t.dir/b.bin
RUN: %run %t.dir/b.bin | FileCheck --check-prefix=B --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/none-win.pas -o %t.dir/n.bin
RUN: %run %t.dir/n.bin | FileCheck --check-prefix=NONE --strict-whitespace --match-full-lines %s
*)

(*
A:a
B:b
NONE:none
*)

//--- a-wins.pas
program p;
{$DEFINE A}
{$DEFINE B}
begin
  {$IFDEF A}
  writeln('a');
  {$ELSEIF B}
  writeln('b');
  {$ELSEIF 0INVALID}
  writeln('c');
  {$ELSE}
  writeln('none');
  {$ENDIF}
end.

//--- b-wins.pas
program p;
{$DEFINE B}
begin
  {$IFDEF A}
  writeln('a');
  {$ELSEIF B}
  writeln('b');
  {$ELSEIF 0INVALID}
  writeln('c');
  {$ELSE}
  writeln('none');
  {$ENDIF}
end.

//--- none-win.pas
program p;
begin
  {$IFDEF A}
  writeln('a');
  {$ELSEIF B}
  writeln('b');
  {$ELSE}
  writeln('none');
  {$ENDIF}
end.
