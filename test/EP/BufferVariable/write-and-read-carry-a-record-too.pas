(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1p 2q
*)

program p;
type rec = record a: integer; b: char end;
var f: file of rec;
    r: rec;
begin
  rewrite(f);
  r.a := 1; r.b := 'p'; write(f, r);
  r.a := 2; r.b := 'q'; write(f, r);
  reset(f);
  read(f, r); write(r.a, r.b, ' ');
  read(f, r); writeln(r.a, r.b)
end.
