(*
A real module, compiled for real so its own writer produces a genuine
dir2/good.pmi, alongside a hand-authored broken good.pmi on an EARLIER
-I search path (dir1) -- the earlier, broken entry must not shadow the
later, working one; -I search order has to keep trying.

RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/dir2/good.pas -o %t.dir/dir2/good.o
RUN: %plang -std=iso10206 -I%t.dir/dir1 -I%t.dir/dir2 %t.dir/prog.pas %t.dir/dir2/good.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

//--- dir2/good.pas
module good;
  function declared: integer;
  begin declared := 7 end;
end.

//--- dir1/good.pmi
not valid pascal ???

//--- prog.pas
program p;
  import good (declared);
begin writeln(declared()) end.
