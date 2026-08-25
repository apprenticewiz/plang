{ ISO §6.10.1(e): a fixed-string-type of capacity c reads up to the line
  terminator; short of c it is padded with spaces (this case) -- see the
  sibling "-long-truncates" file for the past-c truncation case. Sema's
  read-parameter check refused a packed array of char outright (only
  string(n) was accepted), so this never reached codegen to be tested
  before; codegen had no case for it either -- it fell to the scalar path,
  which reads an LLVM array as if it were an integer. (From codegen_test.cpp's
  CodegenStrings.ReadlnIntoAFixedStringTypePadsAndTruncates, split in two.) }

(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 %t.dir/test.pas -o %t
RUN: %run %t < %t.dir/stdin.txt | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hi      ]
*)

//--- test.pas
program p;
var c: packed array[1..8] of char;
begin readln(c); writeln('[', c, ']') end.

//--- stdin.txt
hi
