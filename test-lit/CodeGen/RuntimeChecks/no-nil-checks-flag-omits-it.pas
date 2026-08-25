(*
RUN: %plang -fno-nil-checks %s -o %t
RUN: not --crash %run %t 2> %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: dereference of nil
*)

(*
Without the check the dereference reaches the hardware, so this is a
signal rather than a diagnostic -- which is what the flag asks for.
*)

program p;
type pi = ^integer;
var q: pi;
begin q := nil; writeln(q^) end.
