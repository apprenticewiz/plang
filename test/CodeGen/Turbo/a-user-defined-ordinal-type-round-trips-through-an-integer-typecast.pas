(*
Both directions of a value typecast between a user-defined enum type and a
built-in one: Color(i) reinterprets an Integer's ordinal value as a Color
(checkTypeCast's ordinal<->ordinal rule, via the Symtab TypeAlias lookup
resolveNamed already does for an ordinary type-denoter), and Integer(c)
does the reverse.  Exercises the keyword-token cast arm (Integer, a lexer
keyword) and the identifier cast arm (Color, an ordinary declared type
name) together in the same expression.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
CHECK-NEXT:2
*)

program p;
type
  Color = (Red, Green, Blue);
var
  i: Integer;
  c: Color;
begin
  i := 2;
  c := Color(i);
  writeln(Ord(c));

  i := Integer(c);
  writeln(i);
end.
