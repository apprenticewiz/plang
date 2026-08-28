(*
IFNDEF is IFDEF's negation (its branch runs when the symbol is NOT in
CurrentDefines), and ELSE swaps which branch of an already-decided chain
runs.  Two split-file variants: RELEASE never defined, so IFNDEF's own
branch runs and ELSE is skipped; and RELEASE defined first, so IFNDEF's
branch is the one skipped and ELSE runs instead -- proving ELSE really does
depend on the IFNDEF's own outcome rather than always taking one side.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/release-undefined.pas -o %t.dir/a.bin
RUN: %run %t.dir/a.bin | FileCheck --check-prefix=UNDEF --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/release-defined.pas -o %t.dir/b.bin
RUN: %run %t.dir/b.bin | FileCheck --check-prefix=DEF --strict-whitespace --match-full-lines %s
*)

(*
UNDEF:not-release
DEF:is-release
*)

//--- release-undefined.pas
program p;
begin
  {$IFNDEF RELEASE}
  writeln('not-release');
  {$ELSE}
  writeln('is-release');
  {$ENDIF}
end.

//--- release-defined.pas
program p;
{$DEFINE RELEASE}
begin
  {$IFNDEF RELEASE}
  writeln('not-release');
  {$ELSE}
  writeln('is-release');
  {$ENDIF}
end.
