(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 22
*)

program p(output);
type num = record kind: integer;
       case tag: integer of 1: (a: integer; b: integer); 2: (e: real) end;
var r: num;
begin r.kind := 1; r.tag := 1;
  with r do begin a := 11; b := 22 end;
  with r do writeln(a, ' ', b)
end.
