(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: is undefined
*)

{ EP §6.7.6.2: arg(z) is z's phase angle, atan2(im, re) -- undefined at the
  origin, where every angle is equally valid.  Unguarded, plang_arg let
  std::atan2(0, 0) answer a silent 0 instead of the error the runtime's other
  domain checks (sqrt, ln, a zero complex base raised to a power) already
  give a program instead of an ordinary in-range result -- issue #249. }
program p(output); var c: complex;
begin c := cmplx(0.0, 0.0); writeln(arg(c)) end.
