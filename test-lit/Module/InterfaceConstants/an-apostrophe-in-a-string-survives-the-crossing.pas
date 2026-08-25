(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:it's
*)

//--- mod.pas
module M interface;
export M = (Q);
const Q = 'it''s';
end;
end.

//--- prog.pas
program p(output);
import M;
begin writeln(Q) end.
