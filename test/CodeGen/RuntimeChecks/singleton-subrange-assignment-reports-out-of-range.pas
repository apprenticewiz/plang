(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 4 out of range 5..5
*)

(*
A singleton subrange (Lo == Hi) is still a real Subrange type -- ISO §6.4.7
puts no floor on the interval's width -- so an out-of-range assignment into
one must trap exactly as it would for 1..10. CGAssign.cpp used to skip the
check whenever SubLo == SubHi, which silently accepted any value.
*)

program p;
var x: 5..5; n: integer;
begin
  n := 4; x := n;
  writeln(x:1)
end.
