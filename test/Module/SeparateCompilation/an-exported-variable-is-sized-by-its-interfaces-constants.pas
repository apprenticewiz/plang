(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:70
*)

//--- mod.pas
module M interface;
export M = (n, tab);
const n = 10;
var tab: array[1..n] of integer;
end.
module M;
const n = 2;
var scratch: array[1..n] of integer;
to begin do scratch[1] := 0;
end.

//--- prog.pas
program p(output);
import M;
var i: integer;
begin
  for i := 1 to 10 do tab[i] := i * 7;
  writeln(tab[10]:1)
end.
