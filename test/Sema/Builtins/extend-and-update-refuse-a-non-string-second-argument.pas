(*
RUN: %plang -std=iso10206 -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* extend/update (EP §6.7.5.2) take the same "file, plus an optional
   external filename" shape reset/rewrite do, through the identical
   CodeGen marshalling (StringCallMarshalling::emitCStrArg) in
   CGProcCall::emitCallStmt -- so a non-string second argument crashed the
   compiler here exactly the same way it did for reset/rewrite, rather
   than being refused with a diagnostic. *)

program p;
var f: file [1..10] of integer;
    n: integer;
begin
  extend(f, n);
  update(f, n)
end.

(*
CHECK: 'extend' external file name must be char or string, got 'integer'
CHECK: 'update' external file name must be char or string, got 'integer'
*)
