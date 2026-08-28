(*
TP-only Exit(value): the value is FPC's accepted extension to strict
Delphi's argument-less Exit, legal only inside a FUNCTION -- a procedure has
no result to set.  CurrentRetType is null inside a plain procedure the same
way it is at a program's own top level (checkProcBody resets it there),
matching `fpc -Mtp`'s own "Procedures cannot return a value" for the
identical program (confirmed empirically).
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'exit' may only take a value inside a function; a procedure cannot return one
*)

program p;
procedure NotAFunction;
begin
  Exit(5)
end;
begin
  NotAFunction
end.
