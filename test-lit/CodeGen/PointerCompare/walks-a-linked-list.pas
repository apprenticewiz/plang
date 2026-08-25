(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 3 
*)

program p;
type pn = ^node; node = record v: integer; next: pn end;
var head, q: pn; i: integer;
begin head := nil;
 for i := 3 downto 1 do
  begin new(q); q^.v := i; q^.next := head; head := q end;
 q := head;
 while q <> nil do begin write(q^.v, ' '); q := q^.next end;
 writeln end.
