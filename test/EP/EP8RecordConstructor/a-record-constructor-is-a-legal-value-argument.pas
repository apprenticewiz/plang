(*
Issue #685: a record structured-value-constructor (EP §6.8.7) passed as a
VALUE argument used to fail LLVM IR verification.
CGStructuredValue::emitStructuredValue's record arm builds the constructor
into its own stack temporary and hands back that temporary's ADDRESS (the
same "caller uses memcpy or memcpy-like assign" contract its array sibling
documents just above it) -- correct for an assignment target or a nested
component value, but a value PARAMETER wants the loaded struct itself, not
a pointer to one. StringCallMarshalling::emitCallArg already had a fixup
for this exact shape on an ARRAY parameter (load through the pointer when
the callee's parameter type is an array and the emitted value is a raw
pointer), but no matching case for a record parameter, so
`show(Point[x:1;y:2])` handed the constructor's address straight to a
callee declared over the record's own LLVM struct-by-value type: "Call
parameter type does not match function signature!" -- even though passing
an ordinary record VARIABLE by value (which EmitExpr already loads) worked
fine.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
*)

program p;
type Point = record x, y: integer end;

procedure show(p: Point);
begin
  writeln(p.x, ' ', p.y)
end;

begin
  show(Point[x: 1; y: 2])
end.
