(*
Turbo doesn't replace ISO 7185's digit-sequence label with the identifier
form, it adds the identifier form alongside it -- the same label section,
and the same block, can declare one of each and goto each independently.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:first
CHECK-NEXT:second
CHECK-NEXT:third
*)

program p(output);
label 1, Second;
begin
  writeln('first');
  goto 1;
  writeln('skipped one');
1:
  writeln('second');
  goto Second;
  writeln('skipped two');
Second:
  writeln('third')
end.
