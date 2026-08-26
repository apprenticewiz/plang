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
CHECK:no yes 11 22
*)

//--- instance.pas
program p(output);
type t(n: integer) = record lo: integer; a: array[boolean] of string(n); hi: integer end;
var v: t(5);
begin
  v.lo := 11; v.hi := 22;
  v.a[false] := 'no'; v.a[true] := 'yes';
  writeln(v.a[false], ' ', v.a[true], ' ', v.lo:1, ' ', v.hi:1);
end.

//--- pointer.pas
program p(output);
type t(n: integer) = record lo: integer; a: array[boolean] of string(n); hi: integer end;
var q: ^t;
begin
  new(q, 5);
  q^.lo := 11; q^.hi := 22;
  q^.a[false] := 'no'; q^.a[true] := 'yes';
  writeln(q^.a[false], ' ', q^.a[true], ' ', q^.lo:1, ' ', q^.hi:1);
end.

//--- instance-param.pas
program p(output);
type t(n: integer) = record lo: integer; a: array[boolean] of string(n); hi: integer end;
var a: t(5);
procedure rd(var v: t);
begin
  writeln(v.a[false], ' ', v.a[true], ' ', v.lo:1, ' ', v.hi:1);
end;
begin
  a.lo := 11; a.hi := 22;
  a.a[false] := 'no'; a.a[true] := 'yes';
  rd(a)
end.

//--- pointer-copy.pas
program p(output);
type t(n: integer) = record lo: integer; a: array[boolean] of string(n); hi: integer end;
var q: ^t; v: t(5);
begin
  new(q, 5);
  q^.lo := 11; q^.hi := 22;
  q^.a[false] := 'no'; q^.a[true] := 'yes';
  v := q^;
  writeln(v.a[false], ' ', v.a[true], ' ', v.lo:1, ' ', v.hi:1);
end.
