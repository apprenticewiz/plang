(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true
CHECK-NEXT:true
CHECK-NEXT:false
*)

program p(output);
var f: text;
begin
  rewrite(f); writeln(eof(f));
  writeln(f, 'x'); writeln(eof(f));
  reset(f); writeln(eof(f))
end.
