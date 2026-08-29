(*
Regression test for a bug this item's own testing found (not present before
this item, since ShortString had no concatenation to compute a value with
until now): BuiltinIO.cpp's ShortString write branch used to take its
argument's address with EmitLValue, which has no BinaryExpr case at all --
so `writeln(s + t)`, an ordinary and expected use of this item's own new
concatenation support (CGBinaryOps.cpp), got a null address, hit the
`if (!addr) continue;` guard, and the whole write argument silently
vanished from the output with no diagnostic.  Switched to
StrCall.emitStrAddr, which already falls back to EmitExpr for exactly this
case (see CGExprCore.cpp's ShortString IdentExpr/Index/Field/Deref cases,
which return an address for those too, the same "a string value is carried
by its address" contract VarString already established).

Also covers nested concatenation (`s + t + u`, i.e. `(s + t) + u`) and a
computed concatenation used directly as a comparison operand -- both take
the identical address-of-a-computed-value path.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var s, t, u: string[10];
begin
  s := 'a'; t := 'b'; u := 'c';
  writeln(s + t);
  writeln(s + t + u);
  if (s + t) = 'ab' then writeln('cmp-eq') else writeln('cmp-neq');
end.

(*
CHECK:ab
CHECK-NEXT:abc
CHECK-NEXT:cmp-eq
*)
