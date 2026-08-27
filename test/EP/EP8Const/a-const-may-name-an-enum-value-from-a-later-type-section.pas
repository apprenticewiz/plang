(*
RUN: %plang_ep %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 12
*)

program p(output);
const
  Base = 10;
type
  Color = (Red, Green, Blue);
const
  Favorite = Green;
type
  Dummy = integer;
const
  Other = Base + ord(Blue);
var
  c: Color;
begin
  c := Favorite;
  write(ord(c)); write(' '); writeln(Other)
end.
