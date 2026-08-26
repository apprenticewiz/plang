(*
-dump-parse-tree is Scanner+Parser only -- Sema is never constructed --
proving the wiring test-lit/Parse/'s real parser_test.cpp migration will
build on.  Reuses the same S-expression printer -dump-ast already uses.

RUN: %plang_ir -dump-parse-tree %s | FileCheck %s
*)

program test;
var x : integer;
begin
  x := 42
end.

(*
CHECK: (program test
CHECK: (var (x) integer)
CHECK: (assign x 42)
*)
