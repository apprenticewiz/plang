(*
Issue #640: real Turbo Pascal/`fpc -Mtp` allow the full set of relational
operators (<, <=, >, >=) on pointers, ordering by address, not only = and
<> the way ISO §6.7.2.5 restricts pointer comparison to.  Sema's binary-op
pointer arm (SemaExpr.cpp) used to reject every ordering operator
unconditionally; it is now gated on `!Opts.turbo()`, so under -std=turbo a
pointer ordering comparison compiles and runs, comparing the pointers as
addresses -- confirmed against real `fpc -Mtp`, both for two typed-pointer
operands and for a pointer compared against nil.  See
test/CodeGen/PointerCompare/ordering-operators-are-rejected.pas for the
still-unaffected non-Turbo case, which this does not touch.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:nil not gt
CHECK-NEXT:eq ge le
CHECK-NEXT:gt ge
*)

program p;
var
  p1, p2: PChar;
  buf: array[0..3] of Char;
begin
  p1 := nil;
  if p1 > nil then write('gt ') else write('nil ');
  if p1 > nil then writeln('gt') else writeln('not gt');

  buf := 'abc';
  p1 := buf;
  p2 := buf;
  if p1 = p2 then write('eq ');
  if p1 >= p2 then write('ge ');
  if p1 <= p2 then writeln('le');

  p2 := p1 + 1;
  if p2 > p1 then write('gt ');
  if p2 >= p1 then writeln('ge');
end.
