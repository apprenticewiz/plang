(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8
CHECK-NEXT:42
*)

program p;
function ap(function f: integer): integer;
begin ap := f + 1 end;
function seven: integer; begin seven := 7 end;
procedure outer;
var base: integer;
  function get: integer; begin get := base end;
begin base := 41; writeln(ap(get)) end;
begin writeln(ap(seven)); outer end.
