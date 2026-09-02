(*
Issue #622: 'New' used as a FUNCTION -- 'p := New(PtrType)' -- for a PLAIN
(non-object) pointee, the other real fpc -Mtp -confirmed shape of the
function form besides the object-plus-constructor idiom
(new-used-as-a-function-allocates-stamps-the-vptr-and-runs-the-constructor.pas,
CodeGenTurboConstructors).  No second argument at all here: a bare
'New(PtrType)' is a plain allocation for every pointee kind alike, sized
from PtrType's own domain the same way the statement form's 'new(p)'
already is.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11
CHECK-NEXT:22
CHECK-NEXT:33
*)

program p;
type
  PRec = ^Rec;
  Rec = record a, b, c: integer end;
var
  r: PRec;
begin
  r := New(PRec);
  r^.a := 11; r^.b := 22; r^.c := 33;
  writeln(r^.a); writeln(r^.b); writeln(r^.c);
  Dispose(r);
end.
