(*
CGDebugInfo::enterShadowScope (issue #19) reopens the CURRENT debug
scope as a DILexicalBlock whenever a local/parameter shadows a captured
outer variable -- correct for where that shadowing declaration's own
DILocalVariable attaches, but CodeGenProcs.cpp's procedure-declaration
scoping site used to also read that same currentScope() to decide where
a THIRD nested procedure, declared textually inside the shadowing
activation, attaches its own DISubprogram.  A DISubprogram nested inside
a DILexicalBlock nested inside another DISubprogram is a scope shape
llc's DWARF AsmPrinter cannot handle once any optimization pass runs
(SIGSEGV in DwarfCompileUnit::getOrCreateAbstractSubprogramContextDIE) --
-g -O0 emits the identical metadata shape but never trips this path, so
only -O1 and above ever exercised the crash.  Level2 here both shadows
Level1's own x AND textually contains Level3, the two conditions that
together are needed to produce that scope shape.

This is a compile-and-run check, not an IR-text one: the crash happens
inside llc, downstream of anything -emit-llvm would show, so only an
end-to-end `%plang -g -O1` (etc.) RUN line actually exercises it.

RUN: %plang -g -O0 %s -o %t.O0
RUN: %run %t.O0 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -g -O1 %s -o %t.O1
RUN: %run %t.O1 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -g -O2 %s -o %t.O2
RUN: %run %t.O2 | FileCheck --strict-whitespace --match-full-lines %s
RUN: %plang -g -O3 %s -o %t.O3
RUN: %run %t.O3 | FileCheck --strict-whitespace --match-full-lines %s
*)

program Nest3b;
procedure Level1;
var x: integer;
  procedure Level2;
  var x: integer;
    procedure Level3;
    var y: integer;
    begin y := 3; writeln('level3 y=', y); end;
  begin x := 2; Level3; writeln('level2 x=', x); end;
begin x := 1; Level2; writeln('level1 x=', x); end;
begin Level1; end.

(*
CHECK:level3 y=3
CHECK:level2 x=2
CHECK:level1 x=1
*)
