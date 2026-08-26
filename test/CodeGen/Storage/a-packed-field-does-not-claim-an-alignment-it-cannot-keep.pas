(*
RUN: %plang -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O1 %s -o %t.O1
RUN: %run %t.O1 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -O3 %s -o %t.O3
RUN: %run %t.O3 | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A true 5
*)

(*
ISO Sec6.4.3.1: plang packs a packed record, so its fields sit at byte
offsets that need not satisfy their own types' alignment.  IRBuilder
attaches the ABI alignment of the VALUE TYPE when none is given, so a
set of char (i256, ABI align 16) at offset 1 was stored with align 16 --
a promise about an address nothing had made true.

At -O0 the backend used scalar moves and it ran; from -O1 it emits
movaps and SIGSEGVs.
*)

program p(output);
type pr = packed record c: char; cs: set of char; k: integer end;
var g: pr;
begin g.c := 'A'; g.cs := ['a'..'z']; g.k := 5;
  writeln(g.c, ' ', ('m' in g.cs), ' ', g.k:1) end.
