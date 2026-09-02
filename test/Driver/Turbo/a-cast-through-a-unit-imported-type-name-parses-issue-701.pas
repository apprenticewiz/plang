(*
Issue #701: a cast through a unit-IMPORTED type name (`TEnum(2)`, where
TEnum is declared in a used unit's own interface, not this file) failed to
parse as a cast at all -- Parser::TypeNames_ (see its own comment,
Parser.h) is normally populated only from THIS file's own 'type' sections
(parseTypeDef) plus a handful of seeded predefined Turbo names (issue
#634's own fix, right above this one in Parser.cpp), so a name declared in
a DIFFERENT, used unit's interface was never in it.  `TEnum(2)` fell
through to the ordinary CallExpr parse instead of TypeCastExpr, and Sema
then rejected 'TEnum' as "not callable".

Parser::harvestImportedTypeNames (ParseUnit.cpp), called once per name in a
'uses' clause, now best-effort-loads that unit's published .tui (or
companion .pas) far enough to read the NAMES its interface's own 'type'
section declares, and adds them to TypeNames_ -- exactly the same search
Sema::loadUnitInterfaceExports uses at Sema time, just done once more,
early, at PARSE time, so a cast through an imported name parses as one in
the first place.

RUN: split-file %s %t.dir
RUN: %plang -std=turbo -I%t.dir %t.dir/main.pas -o %t.dir/main
RUN: %run %t.dir/main | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:{{^}}ok{{$}}
*)

//--- serb.pas
unit SerB;
interface
type TEnum = (eA, eB, eC);
implementation
end.

//--- main.pas
program CastP;
uses SerB;
var E: TEnum;
begin
  E := TEnum(2);
  if E = eC then writeln('ok') else writeln('fail');
end.
