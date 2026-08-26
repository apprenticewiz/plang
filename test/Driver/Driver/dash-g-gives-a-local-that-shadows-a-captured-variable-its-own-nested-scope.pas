(*
Issue #19: a nested procedure's closure-capture loop (CodeGenProcs.cpp)
registers every outer variable it can see under the nested procedure's
own DISubprogram before that procedure's own locals are bound.  When a
local shares a captured outer variable's name, the second defVar
silently overwrites the first in CGSymbolTable's own scope map -- so
the GENERATED CODE was already correct; ordinary reads/writes inside
Inner always saw Inner's own x -- but nothing stopped both from also
getting a DILocalVariable under the identical flat DISubprogram scope.
gdb/lldb resolving an unqualified x inside Inner then preferred the
first-declared entry at that scope (the captured, OUTER one) regardless
of which the current PC was actually inside, so a debugger showed 5
where the program itself, and a correct debugger, should show 99.
Confirmed with a real gdb/lldb session (see the PR for issue #19).

Three DILocalVariables named "x" in all: Outer's own (an ordinary
declaration this bug never touched), the capture of it inside Inner
(registered first there, before Inner's own locals are bound), and
Inner's own shadowing local.  The bug was never about a missing
DILocalVariable -- it was about the last two landing in one flat scope.
The checks below skip past Outer's own declaration, capture the
captured-copy's scope and the shadowing declaration's scope, then
require the shadowing declaration's scope to be a lexical block whose
OWN scope is exactly the captured copy's scope -- the DWARF construct a
debugger's innermost-scope-first lookup needs in order to prefer it
over the flatly-scoped outer capture.  Metadata ids are not stable
across compiler changes, hence [[NAME:regex]] capture/back-reference
rather than a hardcoded id.

RUN: %plang_ir -g -std=iso10206 -emit-llvm %s -o %t.ll
RUN: FileCheck %s < %t.ll
*)

program p;
procedure Outer;
  var x: integer;
  procedure Inner;
    var x: integer;
  begin
    x := 99;
    writeln(x)
  end;
begin
  x := 5;
  Inner;
  writeln(x)
end;
begin
  Outer
end.

(*
CHECK: !DILocalVariable(name: "x", scope: [[OUTERSCOPE:![0-9]+]]
CHECK: !DILocalVariable(name: "x", scope: [[CAPTURED:![0-9]+]]
CHECK: !DILocalVariable(name: "x", scope: [[SHADOW:![0-9]+]]
CHECK: [[SHADOW]] = distinct !DILexicalBlock(scope: [[CAPTURED]]
*)
