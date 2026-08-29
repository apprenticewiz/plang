(*
This item's own required minimal scope: a bare `var s: string[N];`
declaration, plus the minimal read/write CodeGen this item adds
(BuiltinIO.cpp's ShortString branches in emitWriteArgs/emitReadArg, backed
by plang_sstr_read/plang_sstr_write in the new runtime/plang_sstr.cpp),
compiles and runs without crashing and produces the value that was read --
not yet Turbo Pascal's exact truncation/comparison/parameter-passing
semantics, which are a separate, later work item this one is the
prerequisite for (see plang_sstr.cpp's own comment for the exact scope
line).

RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:hello
*)

//--- test.pas
program p(input, output);
var s: string[10];
begin
  readln(s);
  writeln(s)
end.

//--- stdin.txt
hello
