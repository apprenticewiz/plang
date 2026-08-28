(*
{$I+}/{$I-} name the IOChecks switch (CompilerSwitches.def: letter 'i',
long name "iochecks") -- not this directive.  Switch dispatch is a separate,
later task (this file's own directory header comments say so throughout;
switchFromLetter/switchFromLongName exist in SwitchTable.h but nothing
calls them yet), so today {$I+}/{$I-} must keep reaching the same
warn_directive_unknown fallback they did before {$I file} existed at all --
dispatchIncludeDirective's own guard (Scanner.h's comment on it) returns
false, having done nothing, for Name "i" with an Argument of exactly '+' or
'-', specifically so this stays true.  Without that guard this would
instead try (and fail) to include a file literally named "+" or "-".
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=UNKNOWN %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
UNKNOWN-DAG: warning: unknown compiler directive 'I'
RAN:ran
*)

program iochecksswitch;
{$I+}
{$I-}
begin
  writeln('ran')
end.
