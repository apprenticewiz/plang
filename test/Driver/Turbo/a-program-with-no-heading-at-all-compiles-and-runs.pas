(*
Turbo drops the 'program name;' heading entirely -- a bare 'begin ... end.'
is a complete program (Parser::parseProgram, ParseDecl.cpp).  ISO 7185
already makes the file-parameter list optional ('program name;' with no
'(...)'); Turbo goes one step further and admits no heading line at all.
The synthesized program name is internal only (it becomes the LLVM
module's identifier) and has no effect on the program's own behavior,
which this test is really checking.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

begin
  writeln('no heading needed');
end.

(*
CHECK:no heading needed
*)
