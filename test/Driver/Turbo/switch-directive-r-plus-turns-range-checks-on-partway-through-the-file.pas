(*
The load-bearing proof that dispatchSwitchDirective actually feeds a real,
position-keyed SwitchTable (Basic/SwitchTable.h) rather than merely
parsing the RANGECHECKS switch and doing nothing with it: an out-of-range
index read under {$R-} is silently let through, and the SAME index,
written after {$R+}, must abort -- if it did not, {$R+} would only be
recognized syntax, not a real switch a query at a later source location
actually sees.

Plain -std=turbo alone keeps checking ON by default (plang's own default,
from the command line's -frange-checks; see CompilerSwitches.def's own
comment on why that is not the same thing as Turbo's native default), so
{$R-} is written first, deliberately, to reach the "unchecked" starting
point this proof needs without depending on -fno-range-checks.  A read,
not a write, for the unchecked access: an out-of-range WRITE with
checking off genuinely scribbles on whatever memory the index lands in,
which is authentic Pascal-with-checking-off behavior but not something a
lit test can depend on landing anywhere in particular.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: before {$R+}: silently out of range under {$R-}, still running
ERR: array index 10 out of bounds 1..3
*)

program switch_r_plus;
var a: array[1..3] of integer;
    i, dummy: integer;
begin
  i := 10;
  {$R-}
  dummy := a[i];
  writeln('before {$R+}: silently out of range under {$R-}, still running');
  {$R+}
  a[i] := 2;
  writeln('unreachable: {$R+} did not take effect')
end.
