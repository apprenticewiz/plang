(*
StringCallMarshalling::emitCallArg's plain-value fallback (the branch that
widens a scalar actual into its formal's declared width) called
CoerceToType with no signedness argument, the same pre-ladder "guess from
the actual's own LLVM width" fallback CGAssign's plain-assignment path
used to fall into (see shortint-assigned-to-a-wider-signed-integer-sign-
extends-issue-177.pas) -- so a negative ShortInt actual passed BY VALUE to
a wider formal zero-extended instead of sign-extending: `s: ShortInt; s :=
-5; show(s)` for `procedure show(x: LongInt)` printed x=251, not -5 (issue
#177).  Fixed by consulting the argument's own Sema-resolved signedness
(exprIsSigned, OrdinalSignedness.h).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:x=-5
CHECK-NEXT:y=-5
*)

program p;
procedure showLong(x: LongInt);
begin
  writeln('x=', x)
end;
procedure showInt(y: Integer);
begin
  writeln('y=', y)
end;
var s: ShortInt;
begin
  s := -5;
  showLong(s);
  showInt(s)
end.
