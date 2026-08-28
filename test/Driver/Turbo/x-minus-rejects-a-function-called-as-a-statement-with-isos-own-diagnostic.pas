(*
The other half of {$X}'s contract: {$X-} puts ISO 7185's own rule back in
force under -std=turbo, refusing a function called as a statement with the
identical err_func_as_statement diagnostic ISO 7185/Extended Pascal always
use for this (Sema::checkUserDefinedCall).
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

program xminususerfunc;
{$X-}
function Foo: integer;
begin
  Foo := 42;
end;
begin
  Foo;
end.

(*
CHECK: 'Foo' is a function; use it in an expression, not as a statement
*)
