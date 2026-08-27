(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: has zero size and cannot be a file's component type
*)

(* Issue #241: get, read and put each derive the file's component stride
   from this type's byte size (zero here), and disagree about what to do
   with a zero stride -- read's fread/fseek(0) paths are no-ops, so eof(f)
   can never become true (an infinite loop), while get falls back to a
   single-character advance regardless, striding through the file as if its
   component were a char. Rejected at the file's declaration instead. *)

program p;
type
  EmptyRecord = record end;
var
  f: file of EmptyRecord;
begin
end.
