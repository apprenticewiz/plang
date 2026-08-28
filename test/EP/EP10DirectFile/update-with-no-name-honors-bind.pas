(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:second
*)

(* issue #411: the same gap plang_extend has also applies to plang_update,
   and reproduces through the fully-standard EP §6.7.5.6 bind() mechanism
   too, not just #239's retained-explicit-name convenience fallback --
   neither function ever called findBinding(), so a name-less update(f)
   after bind(f, b) silently diverted to a fresh, unnamed internal tmpfile
   instead of reopening the bound external file, and whatever it wrote was
   discarded the moment that tmpfile was closed, leaving the real file on
   disk untouched. *)
program p;
var f: bindable text; b: BindingType; s: string(40);
begin
  b.name := 'plang_issue411_update_bind.txt';
  bind(f, b);
  rewrite(f); writeln(f, 'first'); close(f);
  update(f); writeln(f, 'second'); close(f);
  reset(f); readln(f, s); close(f);
  writeln(s)
end.
