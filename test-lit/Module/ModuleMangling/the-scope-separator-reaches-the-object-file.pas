(*
Compiled from a copy in %t.dir, not %s directly: this program declares a
module, and the compiler writes its .pmi beside whatever file it compiled
-- compiling %s in place would write the .pmi into the checked-in source
tree itself, on every test run.

RUN: split-file %s %t.dir
RUN: %plang_ir -std=iso10206 -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK-DAG: @"pas_a$b"
*)

//--- test.pas
module a; function b: integer; begin b := 1 end; end.
program p(output); import a;
begin writeln(b) end.

