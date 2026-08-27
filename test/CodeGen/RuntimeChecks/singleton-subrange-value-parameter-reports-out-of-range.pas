(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 4 out of range 5..5
*)

(*
ISO §6.6.3.2 makes a value parameter a variable of its own that the actual
argument is assigned to, so a singleton-subrange formal must reject an
out-of-range actual exactly as an ordinary assignment would (see
singleton-subrange-assignment-reports-out-of-range.pas). The copy in
CodeGenProcs.cpp mirrored CGAssign.cpp's SubLo == SubHi skip, so `show(4)`
against `x: 5..5` was silently accepted.
*)

program p;
type
    solo = 5..5;
var n: integer;
procedure show(x: solo);
begin
    writeln(x)
end;
begin
    n := 4;
    show(n)
end.
