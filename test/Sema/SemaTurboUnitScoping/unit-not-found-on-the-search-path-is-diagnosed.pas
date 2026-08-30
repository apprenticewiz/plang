(*
Turbo Tier 4, Cluster A item 1: the deliberately temporary unit loader
(Sema::loadUnitInterfaceExports) looks for "<name-lowercased>.pas" on
Opts.ModuleSearchPaths and then in the current directory; naming a unit that
exists nowhere on either is a diagnosed error, not a silent "exports
nothing".

RUN: not %plang -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program NoSuchUnitProg;
uses ThisUnitDoesNotExistAnywhere;
begin
end.

(*
CHECK: error: no unit named 'ThisUnitDoesNotExistAnywhere' was found; it must exist as 'thisunitdoesnotexistanywhere.pas' on the unit search path or in the current directory
*)
