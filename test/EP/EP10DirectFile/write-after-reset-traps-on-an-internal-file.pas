(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(*
ERR: put: file is not open in the required mode
*)

(*
OUT-NOT: should not reach here
*)

(* issue #152: a named file's #124 trap works because reset/rewrite reopen
   it with a single-direction C fopen mode ("r"/"w"), so ferror fires on a
   wrong-direction stdio call.  An internal (unbound) file has no name to
   reopen -- it stays on the same tmpfile(), which glibc always opens "w+b",
   genuinely bidirectional -- so ferror never fires there and the #124 trap
   never sees the violation: put(f) after reset used to succeed at the C
   level and silently overwrite component 2 instead of raising the same
   dynamic-violation a named file raises for the identical sequence. *)
program p;
var f: file of integer;
begin
  rewrite(f);
  f^ := 11; put(f);
  f^ := 22; put(f);
  reset(f);
  get(f);
  f^ := 99; put(f);
  writeln('should not reach here')
end.
