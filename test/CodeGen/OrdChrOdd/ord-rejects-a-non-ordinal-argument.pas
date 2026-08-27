(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
issue #212: ord's argument type-checked into the generic (unvalidated)
fallback in checkCallExpr, so ord(1.5) reached CodeGen with nothing valid
to lower it to -- CGFuncCall.cpp's `ord` case zext's its operand
unconditionally, and the LLVM verifier rejects `zext double ... to i64`,
aborting the compiler with an internal error instead of Sema reporting it.
*)

(*
ERR: requires an ordinal argument
*)

program p;
begin writeln(ord(1.5)) end.
