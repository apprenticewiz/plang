(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: blue
*)

module pal interface;
  export pal = (color, red..green);
  type color = (red, orange, yellow, green, blue);
end.
module pal;
  type color = (red, orange, yellow, green, blue);
end.
program p;
  import pal;
var c: color;
begin c := blue end.
