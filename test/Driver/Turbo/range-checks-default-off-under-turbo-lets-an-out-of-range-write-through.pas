(*
Real Turbo Pascal ships with {$R-}: range checking off by default.  Before
this, LangOptions::RangeChecks defaulted true regardless of Std, so
-std=turbo range-checked exactly like -std=iso7185 unless a program spelled
-fno-range-checks or {$R-} itself -- not what "Turbo Pascal" means.  No
directive and no -f flag here at all: this is the load-bearing
regression-vs-fix distinction the default flip is actually about.

A subrange assignment out of range, not an array index out of range: the
sibling {$R+}/{$R-} switch tests deliberately use a READ rather than a
WRITE for their own "checking is off here" side, since an out-of-range
array WRITE with checking off genuinely scribbles on whatever memory the
index lands in -- authentic Pascal-with-checking-off behavior, but not
something a lit test should depend on landing anywhere in particular.  A
subrange variable has no such concern: it is one scalar's own storage,
sized for the variable's declared type regardless of what value ends up
in it, so storing (and reading back) an out-of-range value is safe and
deterministic with checking off, exactly like the -fno-range-checks
sibling this mirrors (test/CodeGen/RuntimeChecks/
no-range-checks-flag-omits-them.pas) -- except THIS file asks for no flag
at all, so what actually decides the outcome is -std=turbo's own default.

500 truncates to 244 (500 mod 256) rather than passing through unchanged:
TP7 ch.19's storage-width-selection rule (TypeContext::getSubrange,
-std=turbo only) narrows `1..10` to its own bounds' narrowest storage --
an unsigned byte -- rather than inheriting plain Integer's 16 bits the way
it did before that rule existed, and `i := 500; s := i;` with checking off
stores through that byte-wide slot verbatim.  Confirmed against a real
Turbo-Pascal-mode compiler (`fpc -Mtp -R-`), which prints the identical 244.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:written through: 244
*)

program rangechecksdefaultoff;
var
  s: 1..10;
  i: Integer;
begin
  i := 500;
  s := i;
  writeln('written through: ', s);
end.
