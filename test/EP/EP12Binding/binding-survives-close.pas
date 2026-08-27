(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bound
CHECK-NEXT:/tmp/plang_bind_survives_close.txt
*)

(* Issue #248: close(f) must not unbind f -- EP §6.7.5.6 says a binding lasts
   until an explicit unbind, and a later reset/rewrite reopens the same name.
   binding(f).bound used to be read off F->Fp (i.e. "is f currently open?")
   rather than off the binding table, so it went false the moment the file
   was closed even though the name association was still intact. *)
program p;
var f: bindable text; b: BindingType;
begin
  b.name := '/tmp/plang_bind_survives_close.txt';
  bind(f, b);
  rewrite(f);
  close(f);
  b := binding(f);
  if b.bound then writeln('bound') else writeln('unbound');
  writeln(b.name)
end.
