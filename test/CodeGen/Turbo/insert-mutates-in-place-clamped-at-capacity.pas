(*
System-unit string routines item: Insert(source, var s, index) inserts
source into s before position index, MUTATING s in place, clamped at s's own
declared capacity.  Unlike Delete, an out-of-range index IS clamped (to 1, or
to Length(s)+1) rather than making the call a no-op -- confirmed against a
local `fpc -Mtp` install; see plang_sstr_insert's own doc comment
(plang_sstr.cpp) for the full empirically-derived rule, including how a
result that would overflow s's capacity is truncated from the END of the
combined (head + source + tail) result, not from the inserted portion alone.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
  s5: string[5];
begin
  s := 'Hello World';
  Insert('Cruel ', s, 7);
  writeln(s);

  s5 := 'ab';
  Insert('XYZ', s5, 1);
  writeln(s5, ' ', Length(s5));

  s5 := 'abc';
  Insert('XYZW', s5, 2);
  writeln(s5, ' ', Length(s5));

  s := 'Hello';
  Insert('X', s, 100);
  writeln(s);

  s := 'Hello';
  Insert('X', s, 0);
  writeln(s);
end.

(*
CHECK:Hello Cruel World
CHECK-NEXT:XYZab 5
CHECK-NEXT:aXYZW 5
CHECK-NEXT:HelloX
CHECK-NEXT:XHello
*)
