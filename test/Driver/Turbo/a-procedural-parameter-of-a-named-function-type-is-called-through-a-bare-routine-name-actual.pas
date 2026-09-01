(*
Issue #543: real Turbo Pascal idiom requires a NAMED type for a procedural
PARAMETER (`op: BinOp`, with `type BinOp = function(a, b: integer): integer`
declared separately) -- confirmed empirically against a local fpc -Mtp build,
which rejects the ISO/EP inline `op: function(...): integer` spelling written
directly in a parameter list ("Error: Type identifier expected").

CodeGenProcs.cpp's paramMeta_ population used to recognize a procedural
parameter with a bare `llvm::dyn_cast<ProcedureTypeNode>(pg.Type.get())`,
which only matches the inline spelling; a NAMED type reference is a
NamedTypeNode, so the cast always missed and ParamMeta::procType stayed
null for it. CGCallMarshal::marshalArgs's ProcParamArg lookup then answered
null too, so the bare routine-name actual (AddI below) fell into the
ordinary plain-value branch and was evaluated as an implicit
zero-argument call rather than having its address taken -- an LLVM
IR-verification ICE ("Incorrect number of arguments passed to called
function! %call = call i16 @pas_AddI()") rather than a call through ApplyOp
at all.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program IsolateKnownBug;

type
  BinOp = function(a, b: Integer): Integer;

function AddI(a, b: Integer): Integer;
begin
  AddI := a + b;
end;

function ApplyOp(op: BinOp; x, y: Integer): Integer;
begin
  ApplyOp := op(x, y);
end;

begin
  Writeln(ApplyOp(AddI, 3, 4));
end.
