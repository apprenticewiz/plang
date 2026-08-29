(*
Gap 2 regression gate.  Sema::checkDeref's File arm (ISO section 6.5.5's
buffer variable, f^) had no dialect check at all -- any File-typed pointer
operand was accepted unconditionally, so a Turbo program using f^ compiled
silently even though real Turbo Pascal has no buffer variable (it is
replaced by Assign/Seek, the same file-buffer model get/put/page belong to
-- see the sibling test for those).  checkDeref's File arm now refuses f^
under -std=turbo with err_turbo_file_buffer_var, both read (`x := f^`) and
write (`f^ := ...`) positions -- checkDeref does not distinguish direction,
so one dialect check in the shared arm covers both, and the two identical
CHECK lines below (FileCheck matches each forward from where the last one
left off) confirm both call sites actually reach it rather than just one.
*)

(*
RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: error: the file buffer variable ('f^') is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
CHECK: error: the file buffer variable ('f^') is part of Pascal's file-buffer model, which -std=turbo replaces with Assign and Seek
*)

program p;
var
  f: text;
  c: char;
begin
  c := f^;
  f^ := 'x';
end.
