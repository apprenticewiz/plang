(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
issue #212 covered the whole ord/chr/odd family, not just ord: chr shared
the same unvalidated fallback in checkCallExpr, so a non-ordinal argument
(here a two-character string -- a single-quoted one-character literal
denotes Char, which is itself ordinal and would not exercise this) reached
CodeGen's `chr` case, which trunc/ToI64's whatever it is handed -- a
pointer, for a string -- and the LLVM verifier rejects
`zext ptr ... to i64`, aborting the compiler.
*)

(*
ERR: requires an ordinal argument
*)

program p;
begin writeln(chr('xy')) end.
