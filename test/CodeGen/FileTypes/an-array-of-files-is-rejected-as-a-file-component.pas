(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: contains a file type inside a record or array
*)

(* Issue #167: ISO §6.4.3.5 forbids a file's component type from being, or
   containing, a file type -- and that applies just as much when the file is
   reached through an array as when it is the component itself.  The Sema
   check used to only reject a component that was directly a file type, or a
   record whose own immediate field was one; an array of files slipped
   through both of those and was silently accepted. *)

program p;
type
  fa = array[1..3] of text;
var
  f: file of fa;
begin
end.
