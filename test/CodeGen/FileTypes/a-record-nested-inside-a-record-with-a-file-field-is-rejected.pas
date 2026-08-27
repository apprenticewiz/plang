(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: contains a file type inside a record or array
*)

(* Issue #167: a file field need not be on the component record itself to
   violate ISO §6.4.3.5 -- it is just as much "contained" one more record
   down.  The old check only walked the component's own RecordFields one
   level deep, so Outer here (whose own fields are all file-free; the file
   is on Inner, nested inside it) was silently accepted. *)

program p;
type
  Inner = record f: text end;
  Outer = record x: Inner end;
var
  g: file of Outer;
begin
end.
