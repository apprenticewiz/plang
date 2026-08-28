(*
Turbo Pascal allows an ORDINARY IDENTIFIER as a label, on top of -- not
instead of -- ISO 7185's digit-sequence form (Sema.cpp's Phase 1 label loop,
gated on Opts.turbo()).  `goto Done` really has to jump: this is a
compile-and-run test, not just a "it parses" one, so a run that printed
'skipped' would catch a goto that silently fell through to the next
statement instead of actually transferring control.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:before
CHECK-NEXT:reached
*)

program p(output);
label Done;
begin
  writeln('before');
  goto Done;
  writeln('skipped');
Done:
  writeln('reached')
end.
