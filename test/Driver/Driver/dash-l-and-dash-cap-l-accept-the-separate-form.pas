(*
Issue #245: the separate two-token forms "-L dir" and "-l lib" were not
recognized as an option plus its value.  Options.def declared -L and -l
Joined-only, and the driver's hardcoded parser matched only the glued
spelling ("-Ldir" / "-llib"); a standalone "-L" or "-l" token matched
none of parseArgs's dedicated cases, and since -L/-l are Driver-only
options the generic Options.def-driven fallback does not forward either
(that fallback only forwards an option's own spelling to the front end,
which -L and -l have no business reaching anyway) -- so both fell to the
final "unrecognized argument" catch-all.  The token following it then had
no claimant, and parseArgs treated it like any other bare word: an extra
input file.  That produced a filesystem error naming the wrong problem
("is a directory, not a file" / "no such file or directory") instead of
what was actually wrong (an unrecognized flag) -- and never reached the
linker at all.  gcc and clang both accept the separate form; build
systems emit it routinely.

Checked with -### (print the commands without running them) rather than a
real link: what is under test is argument *parsing* -- whether "-L dir"
and "-l lib" are consumed as an option and its value -- not linking, and
-### proves it without requiring the -L path to exist or the -l library to
be resolvable.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang_ir -### %t.dir/hello.pas -L /plang-test-marker-dir -l m -o %t.dir/out > %t.log 2>&1
RUN: FileCheck %s < %t.log
*)

(*
CHECK-NOT: unrecognized argument
CHECK-NOT: is a directory
CHECK-NOT: no such file or directory
CHECK: -L/plang-test-marker-dir
CHECK: -lm
*)

//--- hello.pas
program hello;
begin
  writeln('hi');
end.
