(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: contains a file type inside a record or array
*)

(* Issue #167: EP §6.4.7's schema types are just as able to carry a file
   field as an ordinary record is, and the discriminated record's fields
   live under the type's SchemaBody rather than in its own RecordFields --
   so the old one-record-level-deep RecordFields loop (which only ever saw
   an empty list for a SchemaInstance) could not have caught this shape
   either, independently of the array/record-nesting gaps the other tests
   in this directory cover. *)

program p(output);
type
  Holder(cap: integer) = record f: text; n: integer end;
var
  g: file of Holder(4);
begin
end.
