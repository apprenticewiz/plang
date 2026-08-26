(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abc]
*)

program p(output); function f: string(20); begin f := 'abc' end;
begin writeln('[', f, ']') end.
