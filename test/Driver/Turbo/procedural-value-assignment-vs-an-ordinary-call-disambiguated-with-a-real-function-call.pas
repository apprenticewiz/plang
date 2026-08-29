(*
Tier 2 capstone: 'F := ReadInt' (procedural-VALUE assignment) vs. 'N :=
ReadInt' (an ORDINARY call, ISO Sec6.7.3's implicit zero-argument
function-designator) side by side, disambiguated purely by the ASSIGNMENT
TARGET's own type (Sema::checkRoutineValue / the disambiguation rule cited
in docs/turbo.md's "Procedural types and procedural variables" section) --
not by anything about the spelling on the right.  The existing single-
feature regression
(test/Driver/Turbo/assigning-an-ordinary-functions-own-name-still-means-
call-it-not-take-its-reference.pas) only ever assigns a bare routine name
to an ORDINARY 'f: integer' target; it never puts a genuinely PROCEDURAL
target in the same program to show the two readings side by side, and
never exercises a real, non-trivial function call through the resulting
procedural value afterward.

ReadInt is an ordinary, no-argument Integer function.  'F: TReadFn; F :=
ReadInt' takes a REFERENCE to it (TReadFn is callable, so the target's own
type makes 'ReadInt' on the right a reference, not a call) -- confirmed by
calling THROUGH F afterward (explicit call syntax, 'F()': a bare read of F
with no parentheses is the procedural VALUE itself, which write's own
parameter whitelist correctly refuses to print) and getting ReadInt's real,
changing return value, not some cached one.  'N: Integer; N := ReadInt' calls it immediately
and assigns the result (Integer is never callable) -- the same "assign a
function's own result via its own name" idiom every dialect has always had.
Both appear in the same program, reading the SAME underlying counter that
increments on every real call, so a wrong disambiguation in either
direction changes the numbers printed, not just which code path runs.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:N=1
CHECK-NEXT:viaF=2
CHECK-NEXT:viaF=3
CHECK-NEXT:N=4
*)

program procval_vs_call_disambiguation;
type
  TReadFn = function: Integer;
var
  Counter: Integer;
  N: Integer;
  F: TReadFn;

function ReadInt: Integer;
begin
  Counter := Counter + 1;
  ReadInt := Counter;
end;

begin
  Counter := 0;

  { ordinary call: N gets ReadInt's result immediately }
  N := ReadInt;
  writeln('N=', N);

  { procedural-VALUE assignment: F now holds a reference to ReadInt itself }
  F := ReadInt;
  writeln('viaF=', F());
  writeln('viaF=', F());

  { still an ordinary call afterward -- F's own existence hasn't changed
    what a bare 'N := ReadInt' means }
  N := ReadInt;
  writeln('N=', N);
end.
