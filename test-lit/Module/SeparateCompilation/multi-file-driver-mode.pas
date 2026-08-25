(*
Exercises the driver's own multi-file command-line mode (one `%plang`
invocation given several .pas files directly) rather than the two-step
-c/-I separate-compilation recipe compileTwoFiles uses elsewhere in this
suite -- both are real, independent ways to reach separate compilation.

RUN: split-file %s %t.dir
RUN: cd %t.dir && %plang -std=iso10206 -I. prog.pas mod.pas -o prog
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:49
*)

//--- mod.pas
module Math;
function Square(x: integer): integer;
begin Square := x * x end;
end.

//--- prog.pas
program p;
import Math;
begin writeln(Square(7)) end.
