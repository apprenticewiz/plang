(*
Issue #634: PChar(x)/PAnsiChar(x), used as a CAST EXPRESSION (not a
declared variable's type), failed to parse as a cast at all --
Parser::Parser's TypeNames_ seed (Parser.cpp), which pre-populates the
predefined Turbo type-alias names the parser must recognize as valid cast
targets even though it never sees them declared anywhere (they are
Sema::registerBuiltins-only symbols), listed every other predefined Turbo
type name -- the sized-integer ladder, the loose Boolean family, Single,
AnsiChar, Pointer -- but omitted 'pchar'/'pansichar', the only two Sema
actually registers under exactly that dual (real type + TypeAlias symbol)
scheme (Sema.cpp).  Missing from TypeNames_, `PChar(x)` fell through to the
ordinary CallExpr parse instead of TypeCastExpr, and Sema then rejected
'PChar' as "not callable" -- even though a user-declared alias for the same
underlying type (`type M = PChar; ... M(x)`) parsed and worked fine,
confirming the cast MECHANISM itself was never broken, just this one
seed list's two missing entries.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:ok
*)

program pchar_pansichar_cast;
var
  p: PChar;
  q: PAnsiChar;
  i64: Int64;
begin
  i64 := 0;
  p := PChar(i64);
  q := PAnsiChar(i64);
  if (p = nil) and (q = nil) then writeln('ok') else writeln('fail');
end.
