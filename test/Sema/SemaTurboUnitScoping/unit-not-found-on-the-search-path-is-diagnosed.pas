(*
Turbo Tier 4, Cluster A item 2: Sema::loadUnitInterfaceExports looks for a
published interface file, "<name-lowercased>.tui", then a source file,
"<name-lowercased>.pas", on Opts.ModuleSearchPaths and then in the current
directory; naming a unit that exists as neither anywhere on either is a
diagnosed error, not a silent "exports nothing".

RUN: not %plang -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

program NoSuchUnitProg;
uses ThisUnitDoesNotExistAnywhere;
begin
end.

(*
CHECK: error: no unit named 'ThisUnitDoesNotExistAnywhere' was found; it must exist as 'thisunitdoesnotexistanywhere.tui' (a published unit interface) or 'thisunitdoesnotexistanywhere.pas' (source) on the unit search path or in the current directory
*)
