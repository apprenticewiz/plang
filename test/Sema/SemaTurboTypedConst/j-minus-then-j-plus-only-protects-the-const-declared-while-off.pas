(*
Issue #603, positional transition: Switch::WritableConst is asked with the
ASSIGNMENT's own SourceLocation (Opts.switchOn(Switch::WritableConst, Loc),
SemaStmt.cpp's checkNotProtected), not the declaration's, so what matters
is whether {$J-} is in force at the WRITE, not at the 'const' line. Here
{$J-} covers the first typed constant's declaration, {$J+} then turns
writability back on before the SECOND typed constant is declared, and both
are written afterward under {$J+}: both assignments are accepted, because
both writes happen while the switch reads on, even though `a` was declared
under {$J-}.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK:20
CHECK-NEXT:200
*)

program p;
{$J-}
const a: integer = 2;
{$J+}
const b: integer = 20;
begin
  a := 20;
  b := 200;
  writeln(a);
  writeln(b)
end.
