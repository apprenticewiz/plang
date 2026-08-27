(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
*)

(* issue #287: fopen(path, "r") succeeds on POSIX even when path names a
   directory -- the open() syscall underneath allows O_RDONLY on one, so
   nothing at the C level ever reports a problem. Left unchecked, eof(f)
   comes up true immediately and every subsequent read silently sees what
   looks like an ordinary, unremarkable end-of-file rather than the real
   problem: '/tmp' is not a file at all. This is the same dynamic-violation,
   clean-exit convention issue #150 already established for a missing file
   or a bad directory component (reset-rewrite-extend-update-open-failure-
   exits-cleanly.pas), and the same check the compiler's own driver already
   makes of its *input* file (Driver.cpp's is_directory check). *)

(*
ERR: is a directory
*)

(*
OUT-NOT: should not reach here
*)

program p;
var f: text;
begin
  reset(f, '/tmp');
  writeln('should not reach here')
end.
