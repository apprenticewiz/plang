(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: contains a file type inside a record or array
*)

(* Issue #167: the two ways a file can be "contained" -- through an array,
   and through a record field -- compose.  Neither the direct-file check nor
   the old one-record-level-deep field loop had any array handling at all,
   so an array whose element type is a record with a file field was
   silently accepted. *)

program p;
type
  R = record f: text end;
  ra = array[1..2] of R;
var
  h: file of ra;
begin
end.
