(*
MS-DOS text files traditionally end with a Ctrl-Z, byte value 26, and
Turbo Pascal's scanner -- reading through the DOS text-mode CRT layer, not
raw bytes -- never saw anything written after one.  -std=turbo reproduces
that: scanning stops as if end of file had been reached at the first such
byte.  What follows it below is not valid Pascal at all, a stray backtick
among other things -- if it were reached, this would fail to compile; under
-std=turbo it must never be reached.

RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Eof
*)

x
this text is not reachable Pascal -- a lone backtick ` would otherwise be
an unexpected-character scan error, and the rest is not terminated either
