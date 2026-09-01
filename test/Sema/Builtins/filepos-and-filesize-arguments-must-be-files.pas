(*
RUN: %plang -std=turbo -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* issue #306's fourth slice: FilePos/FileSize's base File-kind check is
   generic now (Builtins.def's AK_File row); the binary-only follow-on check
   (err_binary_file_required) stays hand-written, since it is not a KIND
   every argument shares -- exercised here too, unchanged by the migration. *)
program p;
var i: integer; f: text; r: int64;
begin
  r := filepos(i);
  r := filesize(i);
  r := filepos(f);
  r := filesize(f)
end.

(*
CHECK: 'filepos' requires a file argument, got 'integer'
CHECK: 'filesize' requires a file argument, got 'integer'
CHECK: 'filepos' requires a typed or untyped file, not 'text'
CHECK: 'filesize' requires a typed or untyped file, not 'text'
*)
