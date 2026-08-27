(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value -1 out of range 0..255
*)

(*
issue #166: the negative side of the same missing guard -- chr(-1) truncated
to i8 0xFF, which came back through ord as 255 (two's-complement wrap)
instead of being reported as the out-of-range value it is.
*)

program p;
begin writeln(ord(chr(-1))) end.
