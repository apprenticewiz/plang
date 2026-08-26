(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2
*)

program p(output);
type rec = record a, b: integer end;
var f: file of rec; r: rec;
begin
  rewrite(f, 'tf.bin'); r.a := 1; r.b := 2; write(f, r); close(f);
  reset(f, 'tf.bin'); r.a := 0; r.b := 0; read(f, r); close(f);
  writeln(r.a, ' ', r.b)
end.
