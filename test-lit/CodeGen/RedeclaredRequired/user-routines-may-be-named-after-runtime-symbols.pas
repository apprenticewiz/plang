(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abcde12345
*)

program p(output);
procedure close;   begin write('a') end;
procedure reset;   begin write('b') end;
procedure rewrite; begin write('c') end;
procedure halt;    begin write('d') end;
procedure page;    begin write('e') end;
function  round: integer;  begin round := 1 end;
function  trunc: integer;  begin trunc := 2 end;
function  sqrt: integer;   begin sqrt  := 3 end;
function  sin: integer;    begin sin   := 4 end;
function  ln: integer;     begin ln    := 5 end;
begin
  close; reset; rewrite; halt; page;
  writeln(round:1, trunc:1, sqrt:1, sin:1, ln:1)
end.
