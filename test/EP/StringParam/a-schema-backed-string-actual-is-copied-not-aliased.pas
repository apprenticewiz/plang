(*
Issue #607.  EP §6.4.3.3 makes `string` itself a schema, so a variable
declared `type s(n: integer) = string(n); var a: s(8)` is string-like just
as much as one declared `var a: string(8)` is -- but a's own resolved type
is a SchemaInstance, and StringCallMarshalling::emitCallArg's argIsStrLike
check asked only whether the argument's ResolvedType Kind was directly
VarString, which a SchemaInstance whose body resolves to VarString is not.
The call fell through to the plain EmitExpr(arg) path and handed the
callee's byval-struct parameter the ARGUMENT'S OWN ADDRESS instead of a
copied-by-value struct: an LLVM IR verifier failure ("Call parameter type
does not match function signature") on every such call, not merely a wrong
answer.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcdef]
CHECK-NEXT:abcdef
*)

program p;
type s(n: integer) = string(n);
procedure show(x: string);
begin
  writeln('[', x, ']');
  x := 'clobbered';
end;
var a: s(8);
begin
  a := 'abcdef';
  show(a);
  { show's parameter is a value copy: a itself must be unchanged. }
  writeln(a)
end.
