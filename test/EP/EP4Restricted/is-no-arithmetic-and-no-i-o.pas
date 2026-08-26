(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: grep -c 'error: ' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:3
*)

program p(output);
type k = restricted integer;
var a: k;
begin writeln(a); read(a); writeln(a + 1) end.
