(*
A typed constant declared outside any procedure -- at program scope --
gets real module storage (a GlobalVariable with a compile-time initializer,
emitGlobalTypedConst in CGTypedConst.cpp) the same way an ordinary global
variable does, and, being a variable and not a real constant, may be
assigned to from the program's own top-level statement part.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
CHECK-NEXT:43
*)

const GlobalCount: Integer = 42;
begin
  writeln(GlobalCount);
  GlobalCount := GlobalCount + 1;
  writeln(GlobalCount);
end.
