(*
Turbo Tier 4, Cluster B item 4: LangOptions::UnitSearchPaths (fed by
unitSearchPaths(), the shipped-RTL default) is appended to
Sema::loadUnitInterfaceExports's search list AFTER Opts.ModuleSearchPaths and
".", not before -- a user's own -I directory is meant to shadow the shipped
RTL, not the other way around (see UnitSearchPaths's own comment in
LangOptions.h). Proven here the sharpest way available: two same-named units
that differ in what they export, one reachable only via -I, the other only
via PLANG_UNIT_DIR; the program compiles and runs against the -I copy's
Answer, not the PLANG_UNIT_DIR copy's.

RUN: split-file %s %t.dir
RUN: mkdir -p %t.dir/searchpath %t.dir/rtl
RUN: mv %t.dir/searchpath-unit.pas %t.dir/searchpath/placeholderunit.pas
RUN: mv %t.dir/rtl-unit.pas %t.dir/rtl/placeholderunit.pas
RUN: env PLANG_UNIT_DIR=%t.dir/rtl %plang -std=turbo -I%t.dir/searchpath %t.dir/main.pas -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

//--- searchpath-unit.pas
unit PlaceholderUnit;

interface

const Answer = 1;

implementation

end.

//--- rtl-unit.pas
unit PlaceholderUnit;

interface

const Answer = 2;

implementation

end.

//--- main.pas
program UsesPlaceholderUnit;
uses PlaceholderUnit;
begin
  Writeln(Answer);
end.
