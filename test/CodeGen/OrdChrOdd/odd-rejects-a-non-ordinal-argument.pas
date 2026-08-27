(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
issue #212 covered the whole ord/chr/odd family, not just ord: odd shared
the same unvalidated fallback in checkCallExpr, so a non-ordinal argument
(here a two-character string -- a single-quoted one-character literal
denotes Char, which is itself ordinal and would not exercise this) reached
CodeGen's `odd` case and the same `zext ptr ... to i64` LLVM verifier
rejection aborted the compiler.
*)

(*
ERR: requires an ordinal argument
*)

program p;
begin writeln(odd('xy')) end.
