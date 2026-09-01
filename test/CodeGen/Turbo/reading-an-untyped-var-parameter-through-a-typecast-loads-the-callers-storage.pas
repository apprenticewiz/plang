(*
Issue #645: Sema::checkTypeCast's untyped-var-parameter special case
(`Integer(x)` where `x` is a `var` parameter with no declared type) aliases
the cast's OPERAND ResolvedType to the cast's TARGET type, purely so the
WRITE side (isLValue's TypeCastExpr case) sees a trivial same-size match
with no special case of its own.  That alias made the READ side
(CGExprCore::emitTypeCastValue) unable to tell this apart from an ordinary
same-type scalar conversion: its bothScalar test compared Dst against
Src, but Src now WAS Dst, so it read true and value-evaluated x itself (a
load through x's OWN storage -- for an untyped var param, a bare `ptr`
slot holding the CALLER's address, not the caller's data) instead of
loading through that address at the target's width.  Confirmed on
unmodified main: SIGSEGV at -O0 (an 8-byte load off the end of the
caller's 2-byte Integer global), 'got 0' at -O2 (the caller's address
truncated and reinterpreted as if it were the caller's VALUE).  Real
Turbo/FPC (`fpc -Mtp`) reads 4321 -- this is the canonical
memcmp/memcpy-style untyped-parameter idiom, and it has to actually
work.  The WRITE side (`Integer(x) := 1234`) already worked before this
fix and is exercised here too, to guard against a fix that repairs the
read at the write's expense.

RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;

procedure GetIt(var x);
var
  v: Integer;
begin
  v := Integer(x);
  writeln('got ', v);
end;

procedure SetIt(var x);
begin
  Integer(x) := 1234;
end;

var
  w: Integer;
begin
  w := 4321;
  GetIt(w);

  w := 0;
  SetIt(w);
  writeln('set ', w);
end.

(*
CHECK:got 4321
CHECK-NEXT:set 1234
*)
