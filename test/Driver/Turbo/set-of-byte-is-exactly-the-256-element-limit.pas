(*
Issue #580: 'set of Byte' is a common, ordinary Turbo Pascal idiom -- Byte's
domain is exactly 0..255, 256 values, exactly at plang's own documented
256-element set-representation limit -- but used to be rejected outright
("set base type 'Byte' exceeds the 256-element limit on sets").
checkSetBaseRange's TypeKind::Integer case (SemaType.cpp) now defers to
ordinalRange the same way its Char case already trusts a hardcoded 0..255,
rather than treating every TypeKind::Integer base as unconditionally
unbounded.  Confirmed against real fpc -Mtp, which compiles and runs this
without complaint.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:TRUE TRUE FALSE
*)

program t11;
var
  s: set of Byte;
begin
  s := [];
  Include(s, 255);
  Include(s, 0);
  writeln(255 in s, ' ', 0 in s, ' ', 1 in s);
end.
