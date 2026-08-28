(*
{$WARNING text} and {$HINT text} are the message-directive family's
warning-severity pair (lib/Lex/Directives.cpp): each echoes its own
free-text argument as a real compiler warning, but compilation still
succeeds and the program still runs -- confirmed against real `fpc -Mtp`
field practice, which reports both the same way. Also exercises that
these get real -W names, derived the same way every other warning's is
(DiagnosticsEngine::getWarningName, from warn_directive_warning /
warn_directive_hint): -Werror turns one into a hard failure, and
-Wno-directive-warning turns it off again, same as any other -Wno-<name>.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=WARNINGS %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: not %plang -std=turbo -Werror %s -o %t.werror > %t.werror.out 2>&1
RUN: FileCheck --check-prefix=WERROR %s < %t.werror.out
RUN: test ! -e %t.werror

RUN: %plang -std=turbo -Wno-directive-warning %s -o %t.nowarn > %t.nowarn.out 2>&1
RUN: FileCheck --check-prefix=NOWARN %s < %t.nowarn.out
*)

(*
WARNINGS-DAG: warning: from warning
WARNINGS-DAG: warning: from hint
RAN:ran

WERROR-DAG: error: from warning
WERROR-DAG: error: from hint

NOWARN-NOT: from warning
NOWARN: warning: from hint
*)

program warn_hint;
{$WARNING from warning}
{$HINT from hint}
begin
  writeln('ran')
end.
