(*
The mirror of Turbo's own
test/Driver/Turbo/a-program-with-no-heading-at-all-compiles-and-runs.pas:
outside -std=turbo, the program heading stays exactly as required as
before -- Parser::parseProgram only skips it under Opts.turbo().
*)

(*
RUN: not %plang_ir -dump-parse-tree %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

begin
  writeln('no heading')
end.

(*
CHECK: expected 'program', got 'begin'
*)
