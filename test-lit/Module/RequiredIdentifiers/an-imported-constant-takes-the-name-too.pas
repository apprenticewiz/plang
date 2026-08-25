(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.5
*)

//--- test.pas
module Circle interface;
export Circle = (pi);
const pi = 3.5;
end;
end.
program p(output);
import Circle;
begin writeln(pi:0:1) end.
