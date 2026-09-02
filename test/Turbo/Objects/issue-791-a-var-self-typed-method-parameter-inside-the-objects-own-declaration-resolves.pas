(*
Issue #791 companion: the `var` form of a method parameter naming the
enclosing object type failed identically to the by-value case (companion
file: -by-value-...pas), and docs/turbo.md incorrectly claimed routing
through `var` was already a working workaround -- it wasn't (only a pointer
parameter actually worked around it). Confirmed against a local `fpc -Mtp`
build: 0 errors, compiles and runs correctly.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program SelfParamVar;
type
  TFoo = object
    X: Integer;
    procedure Copy(var Other: TFoo);
  end;
procedure TFoo.Copy(var Other: TFoo);
begin
  X := Other.X;
end;
var a, b: TFoo;
begin
  a.X := 5;
  b.Copy(a);
  writeln(b.X);
end.

(*
CHECK:5
*)
