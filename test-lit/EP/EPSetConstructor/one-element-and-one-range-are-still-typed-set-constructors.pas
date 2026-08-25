(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:one
CHECK-NEXT:range
*)

program p(output);
type cs = set of char;
var s: cs;
begin
  s := cs['a'];      if 'a' in s then writeln('one');
  s := cs['a'..'c']; if 'b' in s then writeln('range')
end.
