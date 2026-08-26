(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p(output);
begin writeln('') end.

(*
CHECK: string constant must contain at least one character
*)
