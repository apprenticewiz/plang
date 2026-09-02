(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

(* Issue #693: page takes zero (defaulting to output) or one file argument
   (ISO 7185 6.6.5.2), but Builtins.def gave it MaxArgs -1 so any number of
   extra arguments were silently ignored.  Now the catalogue says 0,1. *)
program p(output);
begin
  page(output, 1, 'extra', 2.5)
end.

(*
CHECK: 'page' expects 0 or 1 argument(s), got 4
*)
