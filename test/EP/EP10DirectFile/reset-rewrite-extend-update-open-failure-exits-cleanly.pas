(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/reset.pas -o %t.reset
RUN: not %run %t.reset > %t.reset.out 2> %t.reset.err
RUN: FileCheck --check-prefix=RESET-ERR %s < %t.reset.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.reset.out
RUN: %plang -std=iso10206 %t.dir/rewrite.pas -o %t.rewrite
RUN: not %run %t.rewrite > %t.rewrite.out 2> %t.rewrite.err
RUN: FileCheck --check-prefix=REWRITE-ERR %s < %t.rewrite.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.rewrite.out
RUN: %plang -std=iso10206 %t.dir/extend.pas -o %t.extend
RUN: not %run %t.extend > %t.extend.out 2> %t.extend.err
RUN: FileCheck --check-prefix=EXTEND-ERR %s < %t.extend.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.extend.out
RUN: %plang -std=iso10206 %t.dir/update.pas -o %t.update
RUN: not %run %t.update > %t.update.out 2> %t.update.err
RUN: FileCheck --check-prefix=UPDATE-ERR %s < %t.update.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.update.out
*)

(* issue #150: reset/rewrite/extend/update failing to open the external file
   (a bad path in a directory that does not exist, so no permission bit can
   make it succeed) is an ordinary dynamic-violation like every sibling check
   in this file (issue #124's file-wrong-mode trap right next to it, for one)
   -- a clean, scriptable std::exit(PlangRuntimeErrorStatus), not an
   abort()/SIGABRT that dumps core and masquerades as a compiler crash. `not`
   here checks exactly that distinction: it fails the RUN line itself if the
   child process crashed (abort/signal) and only succeeds for an ordinary
   nonzero exit -- so this test would have failed outright against the
   unfixed abort()s. *)

(*
RESET-ERR: cannot open 'no_such_dir_plang_issue150/f.dat' for reading
*)

(*
REWRITE-ERR: cannot open 'no_such_dir_plang_issue150/f.dat' for writing
*)

(*
EXTEND-ERR: cannot open 'no_such_dir_plang_issue150/f.dat' for extend
*)

(*
UPDATE-ERR: cannot open 'no_such_dir_plang_issue150/f.dat' for update
*)

(*
OUT-NOT: should not reach here
*)

//--- reset.pas
program reset_open_failure;
var f: file of integer;
begin
  reset(f, 'no_such_dir_plang_issue150/f.dat');
  writeln('should not reach here')
end.

//--- rewrite.pas
program rewrite_open_failure;
var f: file of integer;
begin
  rewrite(f, 'no_such_dir_plang_issue150/f.dat');
  writeln('should not reach here')
end.

//--- extend.pas
program extend_open_failure;
var f: file of integer;
begin
  extend(f, 'no_such_dir_plang_issue150/f.dat');
  writeln('should not reach here')
end.

//--- update.pas
program update_open_failure;
var f: file of integer;
begin
  update(f, 'no_such_dir_plang_issue150/f.dat');
  writeln('should not reach here')
end.
