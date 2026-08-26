(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[one] 3
CHECK-NEXT:[two-longer] 10
CHECK-NEXT:[x] 1
*)

program p(output);
var f: file of string(10); s: string(10); i: integer;
begin rewrite(f);
  s := 'one'; write(f, s);
  s := 'two-longer'; write(f, s);
  s := 'x'; write(f, s);
  reset(f);
  for i := 1 to 3 do begin read(f, s);
    writeln('[', s, '] ', length(s):1) end end.
