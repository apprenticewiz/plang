(*
Issue #286 (part 2 of 3 -- see the-dry-run-echo... and an-empty-dash-o...
for the other two; -- / - / @response-files are noted in the issue as
things to "consider supporting", not asked for, and are left alone here):
"-o """ -- an -o whose value is the empty string, the way a shell passes
one through from an empty or unset variable ("plang -o "$UNSET" foo.pas")
-- was accepted as if it were a real, if empty, output path.
Opts.outputFile uses "" as its own sentinel for "no -o was given at all"
(see its doc comment in Driver.h), so an explicitly empty -o was
indistinguishable from no -o by the time anything downstream looked at
it, and the pipeline silently fell back to the default output name
(a.out) instead of reporting the mistake -- unlike gcc, which refuses
with "output filename may not be empty".  Rejected as soon as the empty
value is read, in parseArgs, the only place still able to tell the two
cases apart.
*)

(*
RUN: split-file %s %t.dir
RUN: not %plang_ir %t.dir/hello.pas -o "" > %t.out 2>&1
RUN: FileCheck %s < %t.out
*)

(*
CHECK: plang: error: output filename may not be empty
*)

//--- hello.pas
program hello;
begin
  writeln('hi');
end.
