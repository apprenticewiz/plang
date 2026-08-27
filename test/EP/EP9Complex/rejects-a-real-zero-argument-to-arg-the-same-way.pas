(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: is undefined
*)

{ CGFuncCall.cpp's non-complex "arg" path routes a real/integer argument
  through the same plang_arg(re, im) as a complex one, passing im = 0.0 -- so
  a plain real 0 is the origin under the same definition cmplx(0, 0) is, and
  must trap the same way (issue #249), not just when the argument is
  spelled as a complex literal. }
program p(output); var x: real;
begin x := 0.0; writeln(arg(x)) end.
