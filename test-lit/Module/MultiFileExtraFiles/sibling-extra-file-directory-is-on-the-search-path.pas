(*
moduleA/a.pas has to be compiled -- and its .pmi written -- before
moduleB/b.pas, which is why it is listed first here: the issue's own
reproduction order, and the only order that can possibly work.

RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 main.pas moduleA/a.pas moduleB/b.pas -o prog
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

//--- moduleA/a.pas
module A;
function F: integer;
begin F := 1 end;
end.

//--- moduleB/b.pas
module B;
import A;
function G: integer;
begin G := F + 1 end;
end.

//--- main.pas
program p;
import B;
begin writeln(G) end.
