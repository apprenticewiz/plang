(*
RUN: %plang %s -o %t
RUN: %t | FileCheck %s
*)

(*
CHECK-DAG: L3 set a=99
CHECK-DAG: L1 a=99
*)

program p;
procedure L1;
  var a: integer;
  procedure L2;
    procedure L3;
    begin a := 99; writeln('L3 set a=', a) end;
  begin L3 end;
begin
  a := 1;
  L2;
  writeln('L1 a=', a)
end;
begin L1 end.
