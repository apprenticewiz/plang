(*
procParamThunk builds a whole separate function (the trampoline EP
section 6.6.3.1 needs when a procedure is passed as a procedural
parameter) via SetInsertPoint given a raw basic block, which does not
touch the debug location at all -- so the thunk's own instructions inherited
whatever the CALLER's current location happened to be, silently scoped
to the caller's DISubprogram rather than the thunk's own (which does
not exist: the thunk has no Pascal-level source identity, so it gets
none, correctly, once cleared). The verifier does not catch this
specific case -- a function with no DISubprogram at all is not checked
against the scope its instructions claim -- so this was a silent
correctness gap, not a compile failure.

@pas_hello.asparam also appears at its call site (a function-pointer
argument), so the check below has to anchor on "define internal void
@pas_hello.asparam" specifically, not the bare name.  The closing brace
that bounds the CHECK-NOT region below is why this needs split-file --
plang's comment syntax closes a comment on EITHER terminator, so a
literal close-brace character can never appear inside a (* *) block
that's part of what actually gets compiled.

RUN: split-file %s %t.dir
RUN: %plang_ir -g -emit-llvm %t.dir/case.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK: define internal void @pas_hello.asparam
CHECK-NOT: !dbg
CHECK: }
*)

//--- case.pas
program p(output);
var g: integer;

procedure hello;
begin
  g := g + 1
end;

procedure invoke(procedure act);
begin
  act
end;

begin
  g := 0;
  invoke(hello);
  writeln(g)
end.
