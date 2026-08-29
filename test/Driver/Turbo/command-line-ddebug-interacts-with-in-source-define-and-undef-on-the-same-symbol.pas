(*
Neither existing -d/DEFINE test covers a program where BOTH set the same
symbol: command-line-d-and-u-flags-behave-like-in-source-define-and-undef.pas
combines -dDEBUG with -uDEBUG, but both are COMMAND-LINE flags, and the
program itself never writes a DEFINE/UNDEF of its own; define-and-ifdef-
include-the-branch-when-the-symbol-is-defined.pas writes an in-source
DEFINE, but the command line never passes -d/-u for that same run.  This
file passes -dDEBUG on the command line AND has the program's own source
UNDEF and re-DEFINE the very same symbol at different points, proving
-d<symbol> seeds LangOptions::Defines's STARTING set (Frontend.cpp) that an
in-source directive then overrides positionally from there, exactly the way
two in-source directives in that order would -- not a separate, later, or
otherwise privileged kind of "defined" that a program's own directives
cannot reach.

Three IFDEF checks against ONE compile (one -dDEBUG, no -u): the first sees
the command-line seed; an UNDEF between it and the second flips it off; a
DEFINE between the second and third flips it back on.
*)

(*
RUN: %plang -std=turbo -dDEBUG %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:cmdline-on
CHECK-NEXT:after-undef-off
CHECK-NEXT:redefined-on
*)

program p;
begin
  {$IFDEF DEBUG}
  writeln('cmdline-on');
  {$ENDIF}
  {$UNDEF DEBUG}
  {$IFDEF DEBUG}
  writeln('after-undef-on');
  {$ELSE}
  writeln('after-undef-off');
  {$ENDIF}
  {$DEFINE DEBUG}
  {$IFDEF DEBUG}
  writeln('redefined-on');
  {$ENDIF}
end.
