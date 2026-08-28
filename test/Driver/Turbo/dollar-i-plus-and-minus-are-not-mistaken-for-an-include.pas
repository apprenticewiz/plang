(*
{$I+}/{$I-} name the IOChecks switch (CompilerSwitches.def: letter 'i',
long name "iochecks"), not the {$I file}/{$INCLUDE file} directive that
shares its one letter.  dispatchIncludeDirective's own guard (Scanner.h's
comment on it) returns false, having done nothing, for Name "i" with an
Argument of exactly '+' or '-', which is what lets dispatchSwitchDirective
see -- and record -- them instead of dispatchIncludeDirective trying (and
failing) to include a file literally named "+" or "-".

A recognized switch directive is silent: it is recorded into the
position-keyed SwitchTable and nothing more, the same way {$R+}/{$R-} are
(see test/Lex/ScannerTurbo's own switch-directive coverage for the table
itself).  No UNKNOWN warning, no output at all beyond the program's own.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --allow-empty --check-prefix=QUIET %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
QUIET-NOT: note:
QUIET-NOT: warning:
QUIET-NOT: error:
RAN:ran
*)

program iochecksswitch;
{$I+}
{$I-}
begin
  writeln('ran')
end.
