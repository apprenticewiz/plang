(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
begin writeln('a' + 'b') end.

(*
CHECK: operator '+' requires numeric operands, got 'char' and 'char'
*)
