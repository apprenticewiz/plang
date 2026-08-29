(*
Regression test for a bug found while testing this directory's other new
routines: an empty string literal ('') always resolved to EP's own
Type::makeVarString(0), UNCONDITIONALLY, even outside Extended Pascal -- so
under -std=turbo, `s := ''` and `s = ''`/`s <> ''` for a ShortString
variable s failed to even type-check ("cannot assign 'string(0)' to
variable of type 'string[10]'"), since isVarStringLike/VarString are never
what a ShortString operator (assignment, +, comparison) accepts.  Fixed by
giving '' the same EP-vs-not split every OTHER string literal on
checkExpr's StringLitExpr arm already makes (SemaExpr.cpp): TyStr outside
EP, the same placeholder a multi-character literal already gets there,
which every ShortString operator already accepts.

Also exercises the OTHER half of that fix: CGBinaryOps.cpp's sstrOperand/
toSstrPtr lambdas used to floor an empty literal's LENGTH (not just its
alloca capacity) to 1, copying one stray byte out of the literal's own
zero-length interned data for `s + ''`/`s = ''`.  This test's `s + ''`
concatenation and its `<>` comparison would silently misbehave (or read out
of bounds) if that second fix had not also landed.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  s: string[10];
begin
  s := '';
  if s = '' then writeln('empty-ok');
  s := 'hi';
  if s <> '' then writeln('nonempty-ok');
  writeln(s + '', '<end>');
  writeln(Pos('', s));
  writeln(Concat(s, ''), '<end>');
end.

(*
CHECK:empty-ok
CHECK-NEXT:nonempty-ok
CHECK-NEXT:hi<end>
CHECK-NEXT:0
CHECK-NEXT:hi<end>
*)
