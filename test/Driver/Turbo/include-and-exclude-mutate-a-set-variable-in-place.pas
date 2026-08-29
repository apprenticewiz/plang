(*
Include(s, x) / Exclude(s, x) are `s := s + [x]` / `s := s - [x]` by another
name, mutating the set variable s in place rather than building and
reassigning a whole new set value -- CGProcCall's own Include/Exclude arm
reuses the identical set primitives (SetOps::emitSetSingleton/
emitSetBinary) a written-out set literal and `+`/`-` already lower through,
including the same range check against s's own declared base type
(SetOps::declaredRangeOf) a literal member would get.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Green in s
CHECK-NEXT:Blue in s
CHECK-NEXT:Red not in s
CHECK-NEXT:Yellow not in s
*)

program p;
type
  TColor = (Red, Green, Blue, Yellow);
  TColorSet = set of TColor;
var
  s: TColorSet;
begin
  s := [Red];
  Include(s, Green);
  Include(s, Blue);
  Exclude(s, Red);
  if Green in s then writeln('Green in s');
  if Blue in s then writeln('Blue in s');
  if not (Red in s) then writeln('Red not in s');
  if not (Yellow in s) then writeln('Yellow not in s');
end.
