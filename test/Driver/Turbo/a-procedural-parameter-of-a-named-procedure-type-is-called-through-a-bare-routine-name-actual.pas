(*
Issue #543's procedure-typed sibling of the function-typed regression next
to this file (a-procedural-parameter-of-a-named-function-type-...): same
root cause (CodeGenProcs.cpp's paramMeta_ population missed a procedural
parameter declared with a NAMED type, e.g. `p: TIntProc`, because
`llvm::dyn_cast<ProcedureTypeNode>(pg.Type.get())` only ever matched the
inline `function(...): T`/`procedure(...)` spelling, never a NamedTypeNode),
but a procedure rather than a function -- confirmed to crash hard (SIGTRAP,
not merely an LLVM IR-verification error) on the unfixed compiler, since a
void-typed "call result" reached something that dereferenced or otherwise
mishandled it rather than tripping the verifier the way the non-void case
did.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
*)

program ProbeNamedTypeProc;
type
  TIntProc = procedure(a: Integer);
procedure Bump(a: Integer);
begin
  Writeln(a + 1);
end;
procedure Runner(p: TIntProc; x: Integer);
begin
  p(x);
end;
begin
  Runner(Bump, 5);
end.
