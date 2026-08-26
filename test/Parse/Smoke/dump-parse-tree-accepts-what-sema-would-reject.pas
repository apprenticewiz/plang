(*
The whole reason -dump-parse-tree exists rather than reusing -dump-ast for
Parser-level tests: -dump-ast runs Sema too, and fails the same way (empty
stdout, nonzero exit) whether the input has a parse error or is merely
Sema-invalid -- collapsing two different questions into one answer.
-dump-parse-tree stops before Sema, so a program with an undeclared
function name (syntactically fine, semantically wrong) parses cleanly here
where -dump-ast would reject it outright.

RUN: %plang_ir -dump-parse-tree %s | FileCheck %s
RUN: not %plang_ir -dump-ast %s
*)

program p;
var x : integer;
begin x := max(1, 2) end.

(*
CHECK: (call max 1 2)
*)
