(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: 'pp' is read here before
*)

program p(output);
var pp: ^integer;
    j: integer;
procedure q(var x: integer);
begin x := 1 end;
begin
    j := 0;
    if j = 0 then new(pp);
    q(pp^);
    writeln(pp^)
end.
