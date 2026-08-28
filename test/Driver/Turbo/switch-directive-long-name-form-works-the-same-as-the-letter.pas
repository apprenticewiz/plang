(*
CompilerSwitches.def gives every switch two spellings -- the letter
{$R+}/{$R-} uses and the long name {$RANGECHECKS ON}/{$RANGECHECKS OFF}
-- and dispatchSwitchDirective (lib/Lex/Directives.cpp) has to accept
both, since a real Turbo/FPC program may use either.  Same shape as the
sibling {$R+} file's own load-bearing proof, spelled the other way: an
index read under RANGECHECKS OFF is silent, and the same index, written
after RANGECHECKS ON, aborts -- through the new plang_tp_runerror(201)
reporter ("Runtime error 201 at $<addr>", exit 201), the same as that
sibling file now checks for.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %checkexit 201 %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT %s < %t.out
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
OUT: before RANGECHECKS ON: silently out of range, still running
ERR: Runtime error 201 at $
*)

program switch_long_name;
var a: array[1..3] of integer;
    i, dummy: integer;
begin
  i := 10;
  {$RANGECHECKS OFF}
  dummy := a[i];
  writeln('before RANGECHECKS ON: silently out of range, still running');
  {$RANGECHECKS ON}
  a[i] := 2;
  writeln('unreachable: RANGECHECKS ON did not take effect')
end.
