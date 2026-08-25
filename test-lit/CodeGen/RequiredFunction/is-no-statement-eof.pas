{ ISO §6.8.2.3: a procedure-statement names a procedure.  A required function
  written as one used to reach codegen, which called a runtime routine that
  does not exist, or one with the wrong arguments and no diagnostic at all.
  (From codegen_test.cpp's RequiredFunction.IsNoStatement, one loop iteration
  per file: abs/sqr/trim/eof.) }

(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: use it in an expression
*)

program p(output);
var s: string(9);
begin s := 'ab'; eof; writeln(1) end.
