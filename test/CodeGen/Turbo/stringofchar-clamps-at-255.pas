(*
System-unit string routines item: StringOfChar(ch, count) -- count copies of
ch, clamped at 255 (Builtins.def's own comment: StringOfChar's result is
always a capacity-255 ShortString, matching real FPC's own declared
`function StringOfChar(...): string` signature).  A count of 0 gives an
empty string, and a negative count is clamped to 0 the same way Copy/Delete
already clamp a negative count.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
begin
  writeln(StringOfChar('x', 5));
  writeln(StringOfChar('x', 0), '<end>');
  s := StringOfChar('y', 300);
  writeln(Length(s));
end.

(*
CHECK:xxxxx
CHECK-NEXT:<end>
CHECK-NEXT:255
*)
