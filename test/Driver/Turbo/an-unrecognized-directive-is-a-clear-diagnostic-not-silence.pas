(*
A `{$name}` naming no category dispatchDirective knows about (every
directive but the seven message directives, until Cluster B's later items
add conditional compilation / {$I file} / {$R+}-style switches) gets its
own clear "unknown compiler directive" diagnostic rather than silently
doing nothing or being treated as an ordinary, ignored comment -- checked
against real `fpc -Mtp` field practice, which reports an unrecognized
directive as a Warning and keeps compiling (`t.pas(2,2) Warning: Illegal
compiler directive "..."`), not a hard error, which is why plang's own
warn_directive_unknown is a Warning too: the program below still compiles
and runs, and -Wno-directive-unknown -- the -W name
DiagnosticsEngine::getWarningName derives from it -- turns it off, same as
any other -Wno-<name>.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=UNKNOWN %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo -Wno-directive-unknown %s -o %t.quiet > %t.quiet.out 2>&1
RUN: FileCheck --check-prefix=QUIET --allow-empty %s < %t.quiet.out
*)

(*
UNKNOWN: warning: unknown compiler directive 'BOGUS'
RAN:ran

QUIET-NOT: unknown compiler directive
*)

program bogus_directive;
{$BOGUS this directive does not exist}
begin
  writeln('ran')
end.
