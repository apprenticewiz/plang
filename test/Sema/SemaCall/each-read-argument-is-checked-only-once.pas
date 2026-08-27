(*
checkCallStmt's read/readln arm checked every argument once, building a
vector of their types (and, as a side effect, caching each one on
S.Args[I]->ResolvedType).  A later, read-only block -- matching remaining
arguments against a typed file's component type -- called checkExpr on
argument 0 and on every argument after it AGAIN instead of reading the
cached type, so anything wrong with an argument was reported twice
(issue #272).

RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
CHECK: undefined identifier 'nosuchvar'
CHECK-NOT: undefined identifier 'nosuchvar'
COUNT:1
*)

program p;
var f: file of integer;
begin
  read(f, nosuchvar)
end.
