(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p(output);
type dom = record a: integer value 5 end;
     pt = ^dom;
var g: pt;
procedure inner;
type inner_dom = record b: integer value 99 end;
     pt = ^inner_dom;
begin new(g); writeln(g^.a) end;
begin inner end.
