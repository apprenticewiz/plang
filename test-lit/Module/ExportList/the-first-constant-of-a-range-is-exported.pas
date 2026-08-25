(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0
*)

//--- test.pas
module pal interface;
  export pal = (color, red..green);
  type color = (red, orange, yellow, green, blue);
end.
module pal;
  type color = (red, orange, yellow, green, blue);
end.
program p(output);
  import pal;
var c: color;
begin c := red; writeln(ord(c)) end.
