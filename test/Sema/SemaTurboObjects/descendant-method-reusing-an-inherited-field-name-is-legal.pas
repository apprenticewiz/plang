(*
Turbo Tier 5, Cluster A item 1: confirmed against a local fpc -Mtp build
that a descendant object type's METHOD may freely reuse an INHERITED
FIELD's name (compiles clean, no warning even) -- unlike the reverse (a
descendant FIELD reusing an inherited name, field or method, which is
always refused; see descendant-field-reusing-an-inherited-field-name-is-
diagnosed.pas) and unlike two members of the SAME type sharing a name
(duplicate-member-name-within-one-object-type-is-diagnosed.pas, which IS
refused).  This only checks that it compiles; there is nothing here for
-dump-vmt to say about a name collision that, semantically, is not one.
*)

(*
RUN: %plang_ir -std=turbo -dump-vmt %s
*)

program MethodReusesInheritedFieldName;

type
  TA = object
    X: integer;
  end;
  TB = object(TA)
    procedure X;
  end;

procedure TB.X;
begin
end;

begin
end.
