(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:done
*)

program p;
var f: text;
begin
  rewrite(f);
  writeln(f, 'secret');
  writeln('done')
end.
