(*
Issue #286 (part 1 of 3 -- see the-dry-run-echo... and an-empty-dash-o...
for the other two; -- / - / @response-files are noted in the issue as
things to "consider supporting", not asked for, and are left alone here):
the -###/-v echo joined each argument with a plain space and no quoting,
so an argument that itself contained a space was indistinguishable from
two separate arguments once printed, and the line could not be copied back
into a shell and re-run. clang's own -### output quotes (and backslash-
escapes any embedded '"' or '\') every argument for exactly this reason.
Fixed by routing the echo through llvm::sys::printArg the same way clang's
Command::Print does, in place of plang's own ad hoc unquoted join.

-Xlinker's argument is used to inject one argument containing a space,
rather than a source *filename* with a space in it, so that what is under
test -- the echo -- is isolated from split-file's own handling of the
marker line, which is not otherwise exercised by this suite.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang_ir -### %t.dir/hello.pas -Xlinker "an arg" -o %t.dir/out > %t.log 2>&1
RUN: FileCheck %s < %t.log
*)

(*
CHECK: "an arg"
*)

//--- hello.pas
program hello;
begin
  writeln('hi');
end.
