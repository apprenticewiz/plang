(*
procParamThunk builds a whole separate function (the trampoline EP
section 6.6.3.1 needs when a procedure is passed as a procedural
parameter).  It used to be built via SetInsertPoint given a raw basic
block, which did not touch the debug location at all -- so the thunk's
own instructions inherited whatever the CALLER's current location
happened to be, silently scoped to the caller's DISubprogram, because
the thunk itself had no DISubprogram of its own at all. The verifier
did not catch that specific case -- a function with no DISubprogram at
all is not checked against the scope its instructions claim -- so it
was a silent correctness gap, not a compile failure: stepping into a
call made through a procedural parameter (`gdb`, `step`) ran the WHOLE
call to completion, silently skipping over both the thunk and the real
target, rather than entering either.

The fix gives the thunk a real, minimal DISubprogram of its own,
marked DIFlagArtificial (the standard DWARF way to say "this frame
exists but isn't user code, step through it"), and gives each of its
instructions a !dbg pointing into that DISubprogram (line 0 -- a thunk
has no Pascal-level line of its own) instead of either the caller's
stray location or none at all.

@pas_hello.asparam also appears at its call site (a function-pointer
argument), so the check below has to anchor on "define internal void
@pas_hello.asparam" specifically, not the bare name.  The closing brace
that bounds the CHECK region below is why this needs split-file --
plang's comment syntax closes a comment on EITHER terminator, so a
literal close-brace character can never appear inside a (* *) block
that's part of what actually gets compiled.

RUN: split-file %s %t.dir
RUN: %plang_ir -g -emit-llvm %t.dir/case.pas -o %t.ll
RUN: FileCheck %s < %t.ll
*)

(*
CHECK: define internal void @pas_hello.asparam(ptr %0) !dbg [[THUNK_SP:![0-9]+]] {
CHECK-NEXT: entry:
CHECK-NEXT: call void @pas_hello(), !dbg [[THUNK_LOC:![0-9]+]]
CHECK-NEXT: ret void, !dbg [[THUNK_LOC]]
CHECK: }
CHECK: [[THUNK_SP]] = distinct !DISubprogram(name: "pas_hello.asparam"
CHECK-SAME: flags: DIFlagArtificial
CHECK: [[THUNK_LOC]] = !DILocation(line: 0, scope: [[THUNK_SP]])
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
