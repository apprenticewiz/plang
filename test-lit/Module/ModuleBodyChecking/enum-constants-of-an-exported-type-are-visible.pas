(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

//--- test.pas
module M;
  type color = (red, green, blue);
  function code(c: color): integer; begin code := ord(c) end;
end.
program p;
  import M;
var c: color;
begin c := green; writeln(code(c)) end.
