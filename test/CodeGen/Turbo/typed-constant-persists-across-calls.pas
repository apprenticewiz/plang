(*
THE central typed-constant test.  Turbo Pascal's typed constant
(`const X: Integer = 0;`) is not actually a constant at all: TP7 gives it
static storage and initializes it exactly once, at program start.  Declared
inside a procedure, that means it keeps its value across separate calls to
that procedure, the same as a C 'static' local would -- NOT a fresh,
zero/re-initialized stack variable on every activation, which is what every
OTHER local (plain var, local const) already gets (CodeGenProcs.cpp's
emitBlockAllocas/emitBlockDecls).

P is called three times.  If X got a fresh per-activation stack slot instead
of the static storage TP7's own semantics require, this would print 1, 1, 1;
genuinely static storage prints 1, 2, 3.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
CHECK-NEXT:3
*)

procedure P;
const X: Integer = 0;
begin
  X := X + 1;
  writeln(X);
end;

begin
  P;
  P;
  P;
end.
