(*
Recognizing 'TypeName(expr)' as a typecast (parseFactor's and
parseStatement's identifier arms, ParseExpr.cpp/ParseStmt.cpp) is gated on
the identifier actually naming a type (Parser::TypeNames_) -- an ordinary
function name never does, since Pascal never lets a type and a routine
share one scope, so 'Double(21)' keeps parsing and calling exactly as it
always did even in a program that ALSO declares an unrelated type and casts
through it.  This is the regression check for that disambiguation, not just
an assertion that casts and calls can each work in isolation.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
CHECK-NEXT:3
*)

program p;
type
  Color = (Red, Green, Blue);
var
  y: Integer;
  c: Color;

function Double(x: Integer): Integer;
begin
  Double := x * 2;
end;

begin
  y := Double(21);
  writeln(y);

  c := Color(2); { a cast, not a call -- Color is a type, not a routine }
  writeln(Ord(c) + 1);
end.
