(*
Runtime companion to test/Lex/ScannerTurboLiterals/caret-control-after-
equal-in-const-declaration.pas and caret-control-after-of-in-case-
statement.pas (issue #600): the exact repro from that issue -- a `^ctrl`
constant right after '=' in a const-definition, and right after 'of' (and
each ','/';' that follows) in a case-statement's own label list -- both
now parse and evaluate correctly, while a `type PInt = ^Integer`-shaped
pointer type declared right alongside them (also introduced by '=') keeps
meaning exactly what it always has, proving the two uses of '=' coexist
correctly in the same program.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:13
CHECK-NEXT:99
CHECK-NEXT:ctrl-a
CHECK-NEXT:cr
CHECK-NEXT:other
*)

program p;
const
  CR = ^M;
type
  PInt = ^Integer;
var
  n: Integer;
  p: PInt;

  procedure Classify(c: Char);
  begin
    case c of
      ^A: writeln('ctrl-a');
      ^M, ^J: writeln('cr');
    else
      writeln('other')
    end
  end;

begin
  writeln(ord(CR));
  n := 99;
  p := @n;
  writeln(p^);
  Classify(^A);
  Classify(^M);
  Classify('x')
end.
