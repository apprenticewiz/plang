(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:    3.14159265358979
CHECK-NEXT:        0.000000001000
*)

//--- mod.pas
module M interface;
export M = (Pi, Tiny);
const Pi = 3.14159265358979;
      Tiny = 0.000000001;
end;
end.

//--- prog.pas
program p(output);
import M;
begin writeln(Pi:20:14); writeln(Tiny:22:12) end.
