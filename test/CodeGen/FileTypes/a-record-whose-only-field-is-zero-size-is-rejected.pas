(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: has zero size and cannot be a file's component type
*)

(* Issue #241: a record need not be directly empty to have zero size -- one
   whose fields are all themselves zero-size (recursively) is zero-size too,
   and hits the same runtime inconsistency between get and read/eof that a
   directly empty record does.  Sema::byteSizeOf is already recursive over a
   record's fields, so the file-component check catches this shape for free. *)

program p;
type
  Inner = record end;
  Outer = record
    x: Inner
  end;
var
  f: file of Outer;
begin
end.
