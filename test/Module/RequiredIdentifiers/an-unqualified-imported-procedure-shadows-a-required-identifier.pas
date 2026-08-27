(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1005
*)

//--- test.pas
module M;
function abs(x: integer): integer;
begin abs := x + 1000 end;
end.
program p(output);
import M;
begin writeln(abs(5)) end.
