(*
This shape is one of 7 in the SchemaDifferential.EveryLoweringOfOneTypeAgrees
GoogleTest case (test/Driver/module_test.cpp), fanned out one .pas file per
shape rather than kept as one 24-invocation differential loop -- FileCheck
has no native "compare these program outputs to each other" primitive, so
this generalizes the already-shipped -O0..-O3 "N runs must agree" idiom
(test-lit/CodeGen/CodegenOpt/every-level-computes-the-same-answer.pas) by
varying the compiled SOURCE per run instead of the flags: four structurally
different Pascal programs computing one value four different ways --
instance throughout; pointer throughout; instance stores, a schema
parameter loads; pointer stores, a whole-value copy hands it to an instance
which loads -- must all print the identical, hand-verified-correct value.

All 7 shapes here have expectAgree=true in the original GTest source (the
"must still disagree" escape hatch for two now-fixed review-4 defects is
unused by every live shape) -- if a shape's agreement ever regresses again,
the fix is a one-off hand-edit of the affected file, not a reason to bring
back a generic escape hatch or move this content back to GoogleTest.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang_ep %t.dir/instance.pas -o %t.A
RUN: %run %t.A | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep %t.dir/pointer.pas -o %t.B
RUN: %run %t.B | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep %t.dir/instance-param.pas -o %t.C
RUN: %run %t.C | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang_ep %t.dir/pointer-copy.pas -o %t.D
RUN: %run %t.D | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A B 77 hello
*)

//--- instance.pas
program p(output);
type t(n: integer) = record c0: char; p: packed record c: char; x: integer end;
       s: string(n) end;
var v: t(5);
begin
  v.c0 := 'A'; v.p.c := 'B'; v.p.x := 77; v.s := 'hello';
  writeln(v.c0, ' ', v.p.c, ' ', v.p.x:1, ' ', v.s);
end.

//--- pointer.pas
program p(output);
type t(n: integer) = record c0: char; p: packed record c: char; x: integer end;
       s: string(n) end;
var q: ^t;
begin
  new(q, 5);
  q^.c0 := 'A'; q^.p.c := 'B'; q^.p.x := 77; q^.s := 'hello';
  writeln(q^.c0, ' ', q^.p.c, ' ', q^.p.x:1, ' ', q^.s);
end.

//--- instance-param.pas
program p(output);
type t(n: integer) = record c0: char; p: packed record c: char; x: integer end;
       s: string(n) end;
var a: t(5);
procedure rd(var v: t);
begin
  writeln(v.c0, ' ', v.p.c, ' ', v.p.x:1, ' ', v.s);
end;
begin
  a.c0 := 'A'; a.p.c := 'B'; a.p.x := 77; a.s := 'hello';
  rd(a)
end.

//--- pointer-copy.pas
program p(output);
type t(n: integer) = record c0: char; p: packed record c: char; x: integer end;
       s: string(n) end;
var q: ^t; v: t(5);
begin
  new(q, 5);
  q^.c0 := 'A'; q^.p.c := 'B'; q^.p.x := 77; q^.s := 'hello';
  v := q^;
  writeln(v.c0, ' ', v.p.c, ' ', v.p.x:1, ' ', v.s);
end.
