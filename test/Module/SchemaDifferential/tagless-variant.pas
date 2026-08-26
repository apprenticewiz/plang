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
CHECK:5 4242
*)

//--- instance.pas
program p(output);
type t(n: integer) = record a: array[1..n] of integer;
       case boolean of true: (u: integer); false: (w: char) end;
var v: t(2); i: integer;
begin
  for i := 1 to 2 do v.a[i] := i * 5;
  v.u := 4242;
  writeln(v.a[1]:1, ' ', v.u:1);
end.

//--- pointer.pas
program p(output);
type t(n: integer) = record a: array[1..n] of integer;
       case boolean of true: (u: integer); false: (w: char) end;
var q: ^t; i: integer;
begin
  new(q, 2);
  for i := 1 to 2 do q^.a[i] := i * 5;
  q^.u := 4242;
  writeln(q^.a[1]:1, ' ', q^.u:1);
end.

//--- instance-param.pas
program p(output);
type t(n: integer) = record a: array[1..n] of integer;
       case boolean of true: (u: integer); false: (w: char) end;
var a: t(2); i: integer;
procedure rd(var v: t);
var i: integer;
begin
  writeln(v.a[1]:1, ' ', v.u:1);
end;
begin
  for i := 1 to 2 do a.a[i] := i * 5;
  a.u := 4242;
  rd(a)
end.

//--- pointer-copy.pas
program p(output);
type t(n: integer) = record a: array[1..n] of integer;
       case boolean of true: (u: integer); false: (w: char) end;
var q: ^t; v: t(2); i: integer;
begin
  new(q, 2);
  for i := 1 to 2 do q^.a[i] := i * 5;
  q^.u := 4242;
  v := q^;
  writeln(v.a[1]:1, ' ', v.u:1);
end.
