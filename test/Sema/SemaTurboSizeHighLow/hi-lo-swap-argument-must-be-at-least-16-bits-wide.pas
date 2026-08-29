(*
Hi/Lo/Swap all read or rearrange an integer value's own bytes, so the
argument must be one at least 16 bits wide -- an 8-bit Byte/ShortInt has
no separate high and low half to name.  err_hi_lo_swap_argument is
checkCallExpr's dedicated shape check for this (SemaExpr.cpp).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'hi' requires an integer argument of at least 16 bits, got 'Byte'
CHECK: 'lo' requires an integer argument of at least 16 bits, got 'Byte'
CHECK: 'swap' requires an integer argument of at least 16 bits, got 'Byte'
*)

program p;
var
  b: Byte;
  n: Byte;
begin
  b := 5;
  n := Hi(b);
  n := Lo(b);
  n := Swap(b);
end.
