(*
Turbo's own `const` parameter, structured type (record/array/set): CodeGen
must pass it BY REFERENCE -- a plain `ptr` in the callee's own LLVM
signature, exactly like a 'var' parameter's own pointer, but never written
through (Sema already rejects every assignment, see const-parameter-
rejects-whole-and-field-assignment.pas, this feature's Sema twin) -- rather
than copying the whole value in, the way an ordinary value parameter (or
EP's own protected value parameter, which is read-only in exactly the same
way but still fully copied) is lowered.  Checked directly in the IR, in
contrast with an ordinary value parameter of the SAME record type declared
right next to it, so a future change that quietly made const stop being an
efficiency feature (i.e. started copying it in again) would fail here even
though the program's OWN observable behavior -- what this file's second RUN
line also checks -- would look identical either way.

The literal LLVM struct spelling this checks for can only ever appear as
real LLVM output, never as valid Pascal (a bare '}' would close this file's
own (* *) header early -- ISO 7185 Sec6.1.8's "either terminator ends
either", tools/lint_test.py's own check 1) -- so it lives outside the
compiled chunk, split-file style, the same as
test/CodeGen/VariantRecord/a-variant-part-is-aligned-for-the-widest-thing-
in-it.pas already does for an identical reason.

RUN: split-file %s %t.dir
RUN: %plang_ir -std=turbo -emit-llvm %t.dir/test.pas -o %t.ll
RUN: FileCheck %s < %t.ll
RUN: %plang -std=turbo %t.dir/test.pas -o %t
RUN: %run %t | FileCheck --check-prefix=RUNS --strict-whitespace --match-full-lines %s
*)

(*
CHECK: define {{.*}} @{{.*}}TakeConst(ptr {{.*}})
CHECK: define {{.*}} @{{.*}}TakeValue({ i16, i16, i16, i16 } {{.*}})
RUNS:42 99
RUNS-NEXT:42 99
*)

//--- test.pas
program p;
type
  TBig = record
    a, b, c, d: Integer;
  end;

procedure TakeConst(const r: TBig);
begin
  writeln(r.a, ' ', r.d);
end;

procedure TakeValue(r: TBig);
begin
  writeln(r.a, ' ', r.d);
end;

var x: TBig;
begin
  x.a := 42; x.b := 1; x.c := 2; x.d := 99;
  TakeConst(x);
  TakeValue(x);
end.
