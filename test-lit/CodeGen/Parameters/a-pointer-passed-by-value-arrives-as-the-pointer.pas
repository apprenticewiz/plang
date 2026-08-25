(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:734
CHECK-NEXT:734
CHECK-NEXT:734
CHECK-NEXT:99
*)

program p(output);
type iptr = ^integer;
var ip: iptr;
procedure byval(q: iptr);
begin writeln(q^:1); new(q); q^ := 1 end;
procedure byref(var q: iptr);
begin writeln(q^:1); new(q); q^ := 99 end;
begin
  new(ip); ip^ := 734;
  byval(ip); writeln(ip^:1);
  byref(ip); writeln(ip^:1)
end.
