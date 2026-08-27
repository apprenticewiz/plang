(*
A builtin PROCEDURE (writeln, a required identifier with no return type)
called where an expression is expected fell through checkCallExpr's
catch-all builtin-call arm, which answers with `Sym->ReturnType ?  ... :
TyErr` and no diagnostic.  Sym->ReturnType is null for a procedure, so this
produced TyErr silently: Sema recorded no error, the driver believed the
compile had succeeded, and CodeGen -- handed a call to a void builtin in a
context expecting a value -- crashed with a trace/breakpoint trap.  The
user-defined-procedure equivalent (see proc-used-as-expr.pas) already got a
clean diagnostic from checkUserDefinedCall's err_proc_cannot_return_value;
builtins now share it (issue #222).

This needs a real compile (not just -dump-ast) to prove the point: with the
bug still present, Sema's silence meant -dump-ast alone exited 0 -- the
crash only happened once CodeGen saw the malformed call.

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot return a value
*)

program t;
var x: integer;
begin
  x := writeln(5)
end.
