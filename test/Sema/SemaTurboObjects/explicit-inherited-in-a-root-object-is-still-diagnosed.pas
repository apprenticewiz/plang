(*
Issue #624: unlike the bare 'inherited;' form (a legal no-op on a root
object -- see bare-inherited-in-a-root-object-with-no-ancestor-is-a-no-op.pas,
CodeGenTurboVirtualDispatch), an EXPLICIT 'inherited Init;' on a root object
-- one with no ancestor at all -- still names a method there is no ancestor
to look it up on, and stays a clean err_inherited_no_ancestor diagnostic,
confirmed against a local `fpc -Mtp` build ("identifier idents no member").
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program ExplicitInheritedRoot;

type
  TRoot = object
    constructor Init;
  end;

constructor TRoot.Init;
begin
  inherited Init;
end;

begin
end.

(*
CHECK: error: object type 'TRoot' has no ancestor for 'inherited' to reach
*)
