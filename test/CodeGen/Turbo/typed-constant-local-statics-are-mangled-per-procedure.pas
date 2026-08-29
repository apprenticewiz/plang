(*
Two different procedures each declare their own local typed constant called
'Counter'.  A local typed constant gets real (internal-linkage) storage of
its own -- see typed-constant-persists-across-calls.pas -- mangled with the
enclosing procedure's own scope the same way a nested procedure's name is
(namePrefix/PlangScopeSep, CGLinkage.h).  If that mangling were wrong (e.g.
both fell back to the same bare "pasg_Counter" symbol), this would either
fail to link (duplicate symbol) or -- worse -- silently share one counter
between the two unrelated procedures, and Q's first call would already read
1 from P's own prior calls instead of starting at its own 1.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
CHECK-NEXT:2
CHECK-NEXT:1
CHECK-NEXT:3
CHECK-NEXT:2
*)

procedure P;
const Counter: Integer = 0;
begin
  Counter := Counter + 1;
  writeln(Counter);
end;

procedure Q;
const Counter: Integer = 0;
begin
  Counter := Counter + 1;
  writeln(Counter);
end;

begin
  P;
  P;
  Q;
  P;
  Q;
end.
