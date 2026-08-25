(*
The exact same collision as ...local-that-shadows-a-captured-variable...
above, but the shadowing declaration is a parameter rather than a var
local -- both bind through CGSymbolTable::defVar, so both have to go
through the same fix; issue #19 reports both shapes reproduce
identically.  Same three-declarations shape as the local case: Outer's
own parameter x, the capture inside Inner, and Inner's own shadowing
parameter.  See the sibling file's header comment for the full
explanation of what these checks require and why.

RUN: %plang_ir -g -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
procedure Outer(x: integer);
  procedure Inner(x: integer);
  begin
    writeln(x)
  end;
begin
  Inner(99);
  writeln(x)
end;
begin
  Outer(5)
end.

(*
CHECK: !DILocalVariable(name: "x", scope: [[OUTERSCOPE:![0-9]+]]
CHECK: !DILocalVariable(name: "x", scope: [[CAPTURED:![0-9]+]]
CHECK: !DILocalVariable(name: "x", scope: [[SHADOW:![0-9]+]]
CHECK: [[SHADOW]] = distinct !DILexicalBlock(scope: [[CAPTURED]]
*)
