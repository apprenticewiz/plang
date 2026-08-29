(*
System-unit string routines item: Pos(substr, s) -- 1-based index of the
first match, 0 if none.  Critically, Pos('', s) is 0 -- confirmed against a
local `fpc -Mtp` install -- the OPPOSITE of EP's index('', s) = 1 (ISO
10206's own rule for `index`).  This is why Pos is a wholly separate runtime
entry point (plang_sstr_pos, plang_sstr.cpp) from EP's plang_str_index
(plang_str.cpp), never a shared one: reusing EP's `index` for Pos would have
silently given the wrong answer for exactly this case.  See
test/EP/Tier4String/index-function.pas for EP's own index(s, 'ell') = 2 --
note index's OWN argument order is (s, pattern), the reverse of Pos's
(substr, s), each matching its own real dialect's actual signature.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string;
begin
  s := 'Hello, World!';
  writeln(Pos('', s));
  writeln(Pos('', ''));
  writeln(Pos('World', s));
  writeln(Pos('xyz', s));
  writeln(Pos('H', s));
end.

(*
CHECK:0
CHECK-NEXT:0
CHECK-NEXT:8
CHECK-NEXT:0
CHECK-NEXT:1
*)
