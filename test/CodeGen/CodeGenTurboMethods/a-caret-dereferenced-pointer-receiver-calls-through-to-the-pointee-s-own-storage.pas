(*
Turbo Tier 5, Cluster A item 4: 'P^.Method(args)' -- a pointer receiver,
explicitly dereferenced -- passes the SAME address 'P^' itself denotes as
the implicit 'Self' argument, with no object-specific lvalue-emission
logic needed at all: CGExprCore's generic emitLValue already reads P's own
pointer value for a DerefExpr, which IS the pointee's address.  A method
called this way mutates the heap object P points at, and a later call
through the SAME pointer sees the mutation -- proving the address that
traveled as Self was the real one, not a stack copy of *P.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TBox = object
    Value: Integer;
    procedure Put(V: Integer);
    function Get: Integer;
  end;

procedure TBox.Put(V: Integer);
begin
  Value := V;
end;

function TBox.Get: Integer;
begin
  Get := Value;
end;

var
  b: ^TBox;
begin
  New(b);
  b^.Put(7);
  writeln(b^.Get());
  b^.Put(b^.Get() * 6);
  writeln(b^.Get());
  Dispose(b);
end.

(*
CHECK:7
CHECK-NEXT:42
*)
