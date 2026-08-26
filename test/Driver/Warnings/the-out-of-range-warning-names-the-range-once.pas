(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: 'Q' is outside the range 'a'..'z'
ERR-ABSENT-NOT: 97
ERR-ABSENT-NOT: of type
*)

program p(output);
var c: 'a'..'z';
begin c := 'a'; if c = 'b' then c := 'Q'; writeln(c) end.
