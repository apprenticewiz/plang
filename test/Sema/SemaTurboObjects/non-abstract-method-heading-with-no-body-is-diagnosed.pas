(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("Forward declaration not solved") that every non-abstract method heading
-- virtual or not -- needs an out-of-line body somewhere in the same
compilation, exactly like an ordinary ISO §6.6.1 forward declaration's own
completion audit (err_forward_never_defined), just for a method
(err_object_method_never_defined) -- Sema.cpp's Phase 7.6 sibling audit,
run at the end of the block that declared the object type.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program HeadingNeverDefined;

type
  TShape = object
    procedure Draw;
  end;

begin
end.

(*
CHECK: error: method 'TShape.Draw' is declared but never given a defining declaration
*)
