(*
{$MESSAGE text}/{$INFO text}/{$NOTE text} are Turbo's/FPC's informational
compiler directives (lib/Lex/Directives.cpp): each just echoes its own
free-text argument at Info severity while compiling.  Checked against real
`fpc -Mtp` field practice before writing this: {$NOTE}/{$HINT}/{$WARNING}
are genuine FPC directives, {$MESSAGE} is Borland Pascal 7's own (FPC
requires a compound `{$MESSAGE <TYPE> text}` form plang deliberately does
not reproduce -- a bare `{$MESSAGE Hello}` is a plain, unconditional echo
here, matching BP7), and {$INFO} is plang's own addition for symmetry with
the rest of this family (FPC has no standalone {$INFO} that behaves this
way).  None of the three affect the exit code or the program that follows.
*)

(*
RUN: %plang -std=turbo %s -o %t > %t.out 2>&1
RUN: FileCheck --check-prefix=NOTES %s < %t.out
RUN: %run %t | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
NOTES-DAG: note: from message
NOTES-DAG: note: from info
NOTES-DAG: note: from note
RAN:ran
*)

program msg_notes;
{$MESSAGE from message}
{$INFO from info}
{$NOTE from note}
begin
  writeln('ran')
end.
