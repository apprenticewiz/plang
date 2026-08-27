(*
Issue #244: -pc1's own argument parser had the same "-o<file>" (joined,
no space) gap as the driver's -- it recognized only the separate form
("-o file"). A joined "-ofile.ll" fell to the front end's unrecognized-
argument case, OutputFile was never set, and the IR went to stdout
instead of the named file.  This is normally masked when going through
the driver (compile() always constructs -pc1's -o as two separate argv
entries, "-o" and the resolved path, regardless of what the user typed),
so it is checked here by invoking -pc1 directly, the way
test/Driver/PC1's other tests do.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang_ir -pc1 -o%t.dir/direct.ll %t.dir/hello.pas > %t.out
RUN: FileCheck --check-prefix=STDOUT --allow-empty %s < %t.out
RUN: FileCheck --check-prefix=IR %s < %t.dir/direct.ll
*)

(*
STDOUT-NOT: define
*)

(*
IR: define i32 @main
*)

//--- hello.pas
program hello;
begin
  writeln('hi');
end.
