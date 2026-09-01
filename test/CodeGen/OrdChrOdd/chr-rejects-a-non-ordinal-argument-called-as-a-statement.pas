(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
issue #547: the statement-context sibling of
ord-rejects-a-non-ordinal-argument-called-as-a-statement.pas -- chr shares
the exact same checkCallStmt gap ord does.  Before the fix, chr(1.5) called
as a bare statement under Turbo's `{$X+}` (its default) compiled with NO
diagnostic at all: checkCallStmt never called checkBuiltinArgKinds for any
builtin, so the non-ordinal argument sailed straight through to CodeGen
uncaught (chr's own lowering did not happen to crash the way ord's did, so
this was a silent hole rather than an ICE -- see the issue for both halves
of the bug).
*)

(*
ERR: requires an ordinal argument
*)

{$X+}
begin
  chr(1.5);
end.
