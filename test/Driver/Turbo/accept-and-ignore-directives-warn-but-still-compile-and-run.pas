(*
dispatchIgnoredDirective's whole table (lib/Lex/Directives.cpp): a real
Turbo/Borland/FPC directive this milestone does not act on gets its own
warn_directive_ignored -- distinct from warn_directive_unknown, which
would wrongly claim plang has never heard of it -- and the compile still
succeeds and the program still runs, the same way an accepted-but-inert
directive always has (see the sibling message-directive tests).  {$APPTYPE}
(a long name) and {$M ...}/{$D ...} (single letters, with the kind of
argument that is never even looked at -- unlike a switch's Argument, an
ignored directive's is not inspected at all) exercise both halves of the
table.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=IGNORED %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
IGNORED-DAG: warning: compiler directive 'APPTYPE' is recognized but has no effect
IGNORED-DAG: warning: compiler directive 'M' is recognized but has no effect
IGNORED-DAG: warning: compiler directive 'D' is recognized but has no effect
IGNORED-NOT: unknown compiler directive
RAN:ran
*)

program ignored_directives;
{$APPTYPE CONSOLE}
{$M 16384,0,655360}
{$D This is a description string, never inspected}
begin
  writeln('ran')
end.
