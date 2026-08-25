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
CHECK:[K]111x ten chars!
*)

//--- instance.pas
program p(output);
type t(n: integer) = record lead: integer; s: string(n);
       case tag: boolean of
         true:  (c: char; case inner: boolean of
                            true: (d: real); false: (k: char));
         false: (z: integer) end;
var v: t(10);
begin
  v.lead := 111; v.s := 'ten chars!';
  v.tag := true; v.c := 'x'; v.inner := false; v.k := 'K';
  writeln('[', v.k, ']', v.lead:1, v.c, ' ', v.s);
end.

//--- pointer.pas
program p(output);
type t(n: integer) = record lead: integer; s: string(n);
       case tag: boolean of
         true:  (c: char; case inner: boolean of
                            true: (d: real); false: (k: char));
         false: (z: integer) end;
var q: ^t;
begin
  new(q, 10);
  q^.lead := 111; q^.s := 'ten chars!';
  q^.tag := true; q^.c := 'x'; q^.inner := false; q^.k := 'K';
  writeln('[', q^.k, ']', q^.lead:1, q^.c, ' ', q^.s);
end.

//--- instance-param.pas
program p(output);
type t(n: integer) = record lead: integer; s: string(n);
       case tag: boolean of
         true:  (c: char; case inner: boolean of
                            true: (d: real); false: (k: char));
         false: (z: integer) end;
var a: t(10);
procedure rd(var v: t);
begin
  writeln('[', v.k, ']', v.lead:1, v.c, ' ', v.s);
end;
begin
  a.lead := 111; a.s := 'ten chars!';
  a.tag := true; a.c := 'x'; a.inner := false; a.k := 'K';
  rd(a)
end.

//--- pointer-copy.pas
program p(output);
type t(n: integer) = record lead: integer; s: string(n);
       case tag: boolean of
         true:  (c: char; case inner: boolean of
                            true: (d: real); false: (k: char));
         false: (z: integer) end;
var q: ^t; v: t(10);
begin
  new(q, 10);
  q^.lead := 111; q^.s := 'ten chars!';
  q^.tag := true; q^.c := 'x'; q^.inner := false; q^.k := 'K';
  v := q^;
  writeln('[', v.k, ']', v.lead:1, v.c, ' ', v.s);
end.
