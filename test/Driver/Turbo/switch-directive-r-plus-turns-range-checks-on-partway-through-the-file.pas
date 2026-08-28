(*
The load-bearing proof that dispatchSwitchDirective actually feeds a real,
position-keyed SwitchTable (Basic/SwitchTable.h) rather than merely
parsing the RANGECHECKS switch and doing nothing with it: an out-of-range
index read under {$R-} is silently let through, and the SAME index,
written after {$R+}, must abort -- if it did not, {$R+} would only be
recognized syntax, not a real switch a query at a later source location
actually sees.

Plain -std=turbo alone already starts unchecked by default (real Turbo
Pascal's own {$R-} starting point -- see LangOptions::RangeChecks's
dialect-aware default), so {$R-} here is not load-bearing for reaching the
"unchecked" starting point this proof needs -- it is written anyway, to
make the starting point explicit in the source rather than have this
file's own proof depend on which way the command-line default happens to
go.  A read, not a write, for the unchecked access: an out-of-range WRITE with
checking off genuinely scribbles on whatever memory the index lands in,
which is authentic Pascal-with-checking-off behavior but not something a
lit test can depend on landing anywhere in particular.

The abort itself now goes through the new plang_tp_runerror(201) reporter
("Runtime error 201 at $<addr>", exit 201) rather than the shared ISO/EP
plang_err_index ("array index ... out of bounds ...", exit
PlangRuntimeErrorStatus) this file originally checked for -- see
explicit-r-plus-under-turbo-aborts-with-exit-201-not-the-shared-status.pas
for that reporter's own dedicated test.  %checkexit pins the number too,
not just `not`: this file's own point (a real, position-keyed SwitchTable)
is orthogonal to which reporter fires, so there is no reason to leave the
exit code merely "some failure" once checking it exactly is this cheap.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 201 %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: before {$R+}: silently out of range under {$R-}, still running
ERR: Runtime error 201 at $
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
