(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
("Duplicate identifier") that a descendant object type may NOT declare a
field reusing an inherited field's name -- unlike a METHOD, which may
freely reuse an inherited name (hide or override; see
non-virtual-redeclaration-hides-rather-than-overrides-and-warns.pas), a
field has no such "same name, different meaning at this level" concept:
every field, inherited or not, has to share one flat, collision-free
namespace, since it is what memory layout (item 2) hangs offsets off of.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program FieldCollidesWithInherited;

type
  TA = object
    X: integer;
  end;
  TB = object(TA)
    X: string;
  end;

begin
end.

(*
CHECK: error: duplicate member name 'X' in object type 'TB'
*)
