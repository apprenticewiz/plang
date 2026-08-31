(*
Turbo Tier 4, Cluster C item 6: CodeGen's own Dos-intrinsic recognizer
(CGProcCall::tryEmitDosProcCall / CGFuncCall::tryEmitDosFuncCall) is gated
on the call actually having resolved to an export 'uses Dos' brought into
scope (CGLinkage::importOwner returning "dos") -- not on the SPELLING
alone.  A program that never 'uses Dos' at all, and declares its own
ordinary procedure named MkDir, must reach its OWN body untouched: this is
the regression this item's own report worried about most concretely,
since MkDir/ChDir/RmDir/Exec/FindFirst/GetEnv are exactly the six names
the recognizer matches by spelling.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program NoDosOwnMkDir;
procedure MkDir(const S: string);
begin
  Writeln('my own MkDir: ', S);
end;
begin
  MkDir('not a real directory');
end.

(*
CHECK:my own MkDir: not a real directory
*)
