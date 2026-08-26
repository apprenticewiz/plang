(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:102 102
*)

//--- test.pas
module m1;
  var counter: integer;
  procedure bump; begin counter := counter + 1 end;
  function fetch: integer; begin fetch := counter end;
  to begin do counter := 100;
end.
program p;
  import m1 qualified;
begin
  m1.bump; m1.bump;
  writeln(m1.fetch(), ' ', m1.counter)
end.
