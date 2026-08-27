(*
writePMIFiles used to open ModName.pmi and write to it only "if (F)" --
neither an open failure nor a write/flush/close failure was ever checked,
and -c reported success regardless.  A module could therefore "compile"
under -c with no usable interface at all, and the failure only surfaced
later, far from its cause, as a plain "no module named" from whatever
tried to import it.

RUN: split-file %s %t.dir
RUN: chmod 555 %t.dir/rodir
RUN: not %plang -std=iso10206 -c %t.dir/rodir/mod.pas -o %t.dir/mod.o 2> %t.err
RUN: chmod 755 %t.dir/rodir
RUN: FileCheck %s < %t.err
RUN: test ! -e %t.dir/rodir/ro.pmi
*)

(*
CHECK: cannot create module interface file
CHECK-SAME: ro.pmi
*)

//--- rodir/mod.pas
module Ro;
function RoVal: integer;
begin RoVal := 5 end;
end.
