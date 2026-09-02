(*
Issue #624: the bare 'inherited;' no-op carve-out is scoped to STATEMENT
context only (see bare-inherited-in-a-root-object-with-no-ancestor-is-a-no-
op.pas, CodeGenTurboVirtualDispatch) -- confirmed against a local
`fpc -Mtp` build, which still rejects a bare 'inherited' used as a VALUE on
a root object (an "untyped" result used where a real one is required).
Sema::checkInheritedCall's own no-ancestor branch only skips
err_inherited_no_ancestor for 'Method.empty() && !ExpectFunction'; here
ExpectFunction is true (the bare form is the whole right-hand side of the
result assignment), so the ordinary diagnostic still fires.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program BareInheritedRootAsValue;

type
  TRoot = object
    function Foo: Integer;
  end;

function TRoot.Foo: Integer;
begin
  Foo := inherited;
end;

begin
end.

(*
CHECK: error: object type 'TRoot' has no ancestor for 'inherited' to reach
*)
