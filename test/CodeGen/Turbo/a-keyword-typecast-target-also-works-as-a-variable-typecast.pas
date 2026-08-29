(*
The variable-typecast (lvalue) form works with the five keyword type names
too, not only with a user-declared type name -- parseStatement has its own
keyword-token cast arm (TokenKind::Integer/Real/Boolean/Char/String),
separate from the identifier one TByteRec(SomeWord) goes through, and this
is what exercises it. Boolean and Char are both one byte wide, so
Char(B) := Chr(0) reinterprets B's own storage and clears it in place.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:0
*)

program p;
var
  B: Boolean;
begin
  B := True;
  writeln(Ord(B));

  Char(B) := Chr(0);
  writeln(Ord(B));
end.
