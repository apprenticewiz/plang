(*
RUN: split-file %s %t.dir
RUN: %plang -std=iso10206 -c %t.dir/mod.pas -o %t.dir/mod.o
RUN: %plang -std=iso10206 -I%t.dir %t.dir/prog.pas %t.dir/mod.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bound
CHECK-NEXT:{{.*}}bindable-crosses.txt
*)

//--- mod.pas
module BindMod;
type BF = bindable text;
var F: BF;
end.

//--- prog.pas
program p;
import BindMod;
var b, b2: BindingType;
begin
  b.name := 'bindable-crosses.txt';
  bind(F, b);
  rewrite(F);
  b2 := binding(F);
  if b2.bound then writeln('bound') else writeln('unbound');
  writeln(b2.name)
end.
