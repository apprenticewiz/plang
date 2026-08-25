(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
CHECK-NEXT:true true
CHECK-NEXT:true true
*)

program p(output);
begin
  writeln('farka' <= 'farkz', ' ', 'farkz' <= 'farks');
  writeln('abc' < 'abd', ' ', 'abd' > 'abc');
  writeln('abc' = 'abc', ' ', 'abc' <> 'abd')
end.
