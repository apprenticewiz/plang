(*
Turbo Tier 5, Cluster A item 1: real Turbo Pascal has no anonymous object
type -- confirmed against a local fpc -Mtp build ("Anonymous class
definitions are not allowed").  An ObjectTypeNode is only ever resolved as
the direct right-hand side of a 'type Name = object ... end' declaration;
reached any other way (here, a var's own inline type) it is
err_object_type_anonymous.
*)

(*
RUN: not %plang_ir -std=turbo -dump-vmt %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program AnonymousObject;

var
  x: object
    A: integer;
  end;

begin
end.

(*
CHECK: error: object types must be named by a type declaration; an anonymous object type is not allowed
*)
