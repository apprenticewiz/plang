(*
Issue #790: a Turbo unit's own `implementation ... begin ... end.`
initialization section now runs automatically, before the program's own
`begin`, exactly the way real Turbo Pascal/fpc -Mtp already does -- this is
the exact repro from the issue report itself.  Compiled as genuine separate
compilation (the unit's own .pas is deleted before the program compile
below ever runs), proving CodeGen calls into InitUnit.o's own
`__plang_init_initunit` rather than this being some same-translation-unit
special case.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir -c %t.dir/initunit.pas -o %t.dir/initunit.o
RUN: rm %t.dir/initunit.pas
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas %t.dir/initunit.o -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:init ran
CHECK-NEXT:InitRan=99
*)

//--- initunit.pas
unit InitUnit;

interface

var InitRan: Integer;

implementation

begin
  InitRan := 99;
  Writeln('init ran');
end.

//--- main.pas
program MainProg;
uses InitUnit;
begin
  Writeln('InitRan=', InitRan);
end.
