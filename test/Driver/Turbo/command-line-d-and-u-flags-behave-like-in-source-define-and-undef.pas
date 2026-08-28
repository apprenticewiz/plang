(*
-d<symbol>/-u<symbol> (Options.def, parsed independently by both
lib/Driver/Driver.cpp -- generically, via the same Options.def-driven
fallback -Wno-<name> already uses, since the driver has nothing of its own
to do with these beyond forwarding them -- and lib/Frontend/Frontend.cpp,
which turns them into LangOptions::Defines's starting set) are the
command-line spelling of an in-source DEFINE/UNDEF from the start of the
file. Same program compiled four ways: no flag (DEBUG unset), -dDEBUG
(same effect as an in-source DEFINE DEBUG directive), -uDEBUG (a no-op
here, since DEBUG already starts unset -- proves -u does not itself define
anything), and -dDEBUG -u DEBUG together (later flag wins, same as two
in-source directives in that order would).
*)

(*
RUN: %plang -std=turbo %s -o %t.plain
RUN: %run %t.plain | FileCheck --check-prefix=OFF --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo -dDEBUG %s -o %t.defined
RUN: %run %t.defined | FileCheck --check-prefix=ON --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo -uDEBUG %s -o %t.undef_noop
RUN: %run %t.undef_noop | FileCheck --check-prefix=OFF --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo -dDEBUG -uDEBUG %s -o %t.define_then_undef
RUN: %run %t.define_then_undef | FileCheck --check-prefix=OFF --strict-whitespace --match-full-lines %s
*)

(*
OFF:done
ON:debug
ON-NEXT:done
*)

program p;
begin
  {$IFDEF DEBUG}
  writeln('debug');
  {$ENDIF}
  writeln('done')
end.
