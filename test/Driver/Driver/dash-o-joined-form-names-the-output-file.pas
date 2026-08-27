(*
Issue #244: -o<file> (joined, no space -- "-ojoined.o") was silently
ignored, and the pipeline fell back to the default output name instead.
Options.def already declared -o JoinedOrSeparate, but the driver's own
hardcoded argument parser matched only the separate form ("-o file"); a
joined "-ojoined.o" fell through every dedicated case in parseArgs to the
generic Options.def-driven fallback, which (since -o is a "Both" option)
forwarded the whole, still-prefixed string to the -pc1 front end as an
opaque frontend argument rather than recognizing it as -o's own value --
and -pc1's own parser had the identical separate-only gap, so it rejected
the forwarded "-ojoined.o" as unrecognized too.  Neither parser ever set
Opts.outputFile / OutputFile, so both silently fell back to the default
name (hello.o / a.out).

Checked in -c (object) mode, where the fix is exercised through the
driver's own defaulting logic (llc's -o comes straight from
Opts.outputFile, never through -pc1's parser), and in the default
(executable) mode, which additionally runs the link step.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang_ir -c %t.dir/hello.pas -o%t.dir/joined.o
RUN: %plang_ir %t.dir/joined.o -o %t.dir/joined_from_o
RUN: %run %t.dir/joined_from_o | FileCheck --strict-whitespace --match-full-lines %s

RUN: %plang_ir %t.dir/hello.pas -o%t.dir/joined_bin
RUN: %run %t.dir/joined_bin | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hi
*)

//--- hello.pas
program hello;
begin
  writeln('hi');
end.
