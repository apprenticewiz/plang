(*
Issue #125: plang could not link pre-compiled .o files without at least
one .pas source on the command line.  parseArgs already routed .o/.a
arguments into Opts.linkerArgs, but Driver::run() unconditionally
rejected the invocation with "no input files" whenever Opts.inputFile
was empty -- even when linkerArgs held real, linkable objects.  A plain
"plang foo.o -o foo_bin" invocation, the standard "compile with -c, link
separately" workflow every C toolchain supports, should succeed.

Also exercises linking two .o files together with no .pas input at all
(hello.o alone would not prove the multi-object path, since a self
contained program has nothing a second .o could still be missing), so
the fix is proven for more than the single-object case.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -c %t.dir/hello.pas -o %t.dir/hello.o
RUN: %plang %t.dir/hello.o -o %t.dir/hello_alone
RUN: %run %t.dir/hello_alone | FileCheck --check-prefix=ALONE --strict-whitespace --match-full-lines %s
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir -c %t.dir/prog.pas -o %t.dir/prog.o
RUN: %plang %t.dir/prog.o %t.dir/mod.o -o %t.dir/prog_linked
RUN: %run %t.dir/prog_linked | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
ALONE:hi
CHECK:49
*)

//--- hello.pas
program hello;
begin writeln('hi') end.

//--- mod.pas
module Math;
function Square(x: integer): integer;
begin Square := x * x end;
end.

//--- prog.pas
program p;
import Math;
begin writeln(Square(7)) end.
