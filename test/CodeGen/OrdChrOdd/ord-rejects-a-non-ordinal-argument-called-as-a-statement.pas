(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
issue #547: ord-rejects-a-non-ordinal-argument.pas (this directory) covers
ord(1.5) called in EXPRESSION position, which checkCallExpr's own
checkBuiltinArgKinds call already caught (issue #212).  Turbo's `{$X+}`
(its default) additionally lets a required FUNCTION be called as a bare
STATEMENT, its result discarded -- a wholly separate dispatch path
(Sema::checkCallStmt, SemaStmt.cpp) that, until issue #547 was fixed, had
NO argument-kind checking at all, for any builtin.  `ord(1.5);` written as
a statement reached CodeGen with nothing valid to lower it to -- the same
unconditional `zext double ... to i64` ord-rejects-a-non-ordinal-argument.pas
exercises in expression position -- and aborted the compiler with an LLVM
IR verifier failure instead of Sema reporting it.
*)

(*
ERR: requires an ordinal argument
*)

{$X+}
begin
  ord(1.5);
end.
