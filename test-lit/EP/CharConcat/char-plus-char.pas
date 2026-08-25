(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abc]
*)

program p(output); var s: string(10); c: char;
begin c := 'c'; s := 'a' + 'b' + c; writeln('[', s, ']') end.
