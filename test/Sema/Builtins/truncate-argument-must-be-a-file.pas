(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: Truncate's File-kind check is generic now
   (Builtins.def's AK_File row) -- split out of the hand-written arm it used
   to share with Seek (which stays hand-written: its own second argument, a
   record number, is a different kind).  Also exercises the still
   hand-written binary-only follow-on check (err_binary_file_required),
   unchanged by the split. *)
program p;
var i: integer; f: text;
begin
  truncate(i);
  truncate(f)
end.

(*
CHECK: 'truncate' requires a file argument, got 'integer'
CHECK: 'truncate' requires a typed or untyped file, not 'text'
*)
