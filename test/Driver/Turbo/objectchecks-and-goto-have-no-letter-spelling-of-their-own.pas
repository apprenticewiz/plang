(*
ObjectChecks and Goto (CompilerSwitches.def) are recognized only by their
long names, confirmed against fpc's own compiler source to have no
single-letter spelling in real Turbo/FPC at all -- see that file's own
comment.  `{$OBJECTCHECKS ON}`/`{$GOTO OFF}` must be silently recorded
(the same silence `{$R+}` gets), and 'O' and 'G' must stay exactly what
they were before either switch existed: entries on the accept-and-ignore
table (Overlays, imported data), never mistaken for ObjectChecks/Goto just
because they happen to be the first letter of the long name.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=CHECK %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
CHECK-DAG: warning: compiler directive 'O' is recognized but has no effect
CHECK-DAG: warning: compiler directive 'G' is recognized but has no effect
CHECK-NOT: unknown compiler directive
RAN:ran
*)

program no_letter_switches;
{$OBJECTCHECKS ON}
{$GOTO OFF}
{$O+}
{$G+}
begin
  writeln('ran')
end.
