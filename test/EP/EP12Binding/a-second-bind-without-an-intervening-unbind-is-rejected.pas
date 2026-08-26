(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: already bound
*)

program p;
var f: bindable text; b: BindingType;
begin
  b.name := '/tmp/plang_bind_twice_a.txt';
  bind(f, b);
  b.name := '/tmp/plang_bind_twice_b.txt';
  bind(f, b)
end.
