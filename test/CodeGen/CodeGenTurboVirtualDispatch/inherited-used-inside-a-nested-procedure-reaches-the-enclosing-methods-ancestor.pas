(*
Issue #625: an explicit 'inherited Method(...)' written inside a NESTED
procedure declared within a method body still names the ENCLOSING method's
own ancestor-resolution context -- Sema used to require CurrentProc itself
(the nested procedure) to be a method with its own OwnerType, which a
nested procedure never has, so this was rejected outright even though
'Self' -- and every field of the enclosing method's own owning type -- is
already visible from inside the same nested procedure, through the same
symbol-table scope pushMethodSelfScope leaves in place for the duration of
the enclosing method's body.

Confirmed against a local `fpc -Mtp` build: fpc accepts this and forwards
the enclosing activation's own Self into the ancestor call, giving the
exact output below.
*)

(*
RUN: %plang -std=turbo -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -std=turbo -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
type
  TBase = object
    function Foo(x: integer): integer;
  end;
  TDerived = object(TBase)
    function Foo(x: integer): integer;
  end;

function TBase.Foo(x: integer): integer;
begin
  Foo := x + 1;
end;

function TDerived.Foo(x: integer): integer;
  function Nested(y: integer): integer;
  begin
    Nested := inherited Foo(y) + 100;
  end;
begin
  Foo := Nested(x);
end;

var
  d: TDerived;
begin
  writeln(d.Foo(5));
end.

(*
CHECK:106
*)
