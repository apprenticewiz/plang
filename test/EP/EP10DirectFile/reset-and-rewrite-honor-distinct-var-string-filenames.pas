(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:111
CHECK-NEXT:222
*)

(* EP §6.7.5.2: reset/rewrite's optional second argument names the external
   file, and a string(n) actual carries that name as a length-then-bytes
   record rather than the null-terminated bytes the file-open runtime call
   takes.  Two var-string filenames of the same length, used for two
   different files, must still open two different files -- not collide on
   whatever a raw pointer to that record happens to read as a C string. *)

program p;
var f: file of integer;
    n1, n2: string(20);
    v: integer;
begin
  n1 := 'aaa.dat';
  n2 := 'bbb.dat';
  rewrite(f, n1); v := 111; write(f, v); close(f);
  rewrite(f, n2); v := 222; write(f, v); close(f);
  reset(f, n1); read(f, v); writeln(v); close(f);
  reset(f, n2); read(f, v); writeln(v); close(f)
end.
