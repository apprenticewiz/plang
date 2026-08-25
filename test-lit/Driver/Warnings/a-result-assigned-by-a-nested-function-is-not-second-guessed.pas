(*
RUN: %plang %s -o %t 2> %t.compile.err
RUN: %run %t > %t.out 2> %t.run.err
RUN: cat %t.compile.err %t.run.err > %t.err
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: every path
*)

program p(output);
function outer: integer;
var t: integer;
  function inner: integer;
  begin outer := 42; inner := 0 end;
begin t := inner end;
begin writeln(outer) end.
