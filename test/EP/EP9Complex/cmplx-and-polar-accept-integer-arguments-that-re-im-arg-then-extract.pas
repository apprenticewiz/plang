(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

{ Regression companion to the two argument-type-rejection tests alongside
  this one (re-im-arg-require-a-genuinely-complex-argument.pas and
  cmplx-and-polar-reject-a-non-numeric-or-complex-argument.pas, both issue
  #306): checking cmplx/polar's arguments and re/im/arg's argument must not
  over-tighten the ordinary integer-to-real widening every real-type
  parameter gets elsewhere in the language.  cmplx's own two arguments may
  be any mix of integer and real (here: one of each, then two plain
  integers through polar), and re/im/arg still extract from the genuinely
  complex result the same as they do from a real-literal-only construction
  in cmplx-constructor.pas/polar-constructor.pas. }

program p;
var a, b: complex;
begin
  a := cmplx(3, 4.0);
  writeln(re(a):1:1);
  writeln(im(a):1:1);
  b := polar(1, 0);
  writeln(re(b):1:1);
  writeln(im(b):1:1);
  writeln(arg(b):1:4)
end.

(*
CHECK:3.0
CHECK-NEXT:4.0
CHECK-NEXT:1.0
CHECK-NEXT:0.0
CHECK-NEXT:0.0000
*)
