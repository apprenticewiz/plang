(*
Turbo Tier 5, Cluster A item 0 is parsing only: no ancestor resolution, no
VMT/layout, no method-body type-checking (that is items 1-7's job -- see
ObjectTypeNode's own comment in AstDecl.h).  A parsed ObjectTypeNode
reaching Sema's type resolution today is a real, if temporary, terminal
case, so it gets a dedicated diagnostic (err_object_type_not_yet_supported)
instead of silently miscompiling or crashing -- the same
"land the syntax first" placeholder shape Tier 4 item 0 left for a
parsed-but-unprocessed UnitNode.
*)

(*
RUN: not %plang_ir -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program NotYetSupported;

type
  TShape = object
    procedure Draw; virtual; abstract;
  end;

begin
end.

(*
CHECK: object types are not yet supported
*)
