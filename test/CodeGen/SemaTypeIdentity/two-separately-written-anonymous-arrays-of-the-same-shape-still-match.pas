(*
Issue #178's control case: a NAMED array type-denoter (`type X = array...`)
is nominal now -- see the sibling tests in this directory -- but an array
type written inline, with no `type` declaration of its own, has no
declaration to be identified by and stays exactly as structural as it always
was.  Two separately-written `array[1..5] of Integer` denoters, neither one
reached through a `type` declaration, must still be assignment-compatible;
over-tightening this alongside the named-type fix would be a real, different
bug (and one with no Record/Enum precedent either -- an anonymous record or
enumerated-type is likewise compared structurally, never made nominal, no
matter how it is written).

RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
var a: array[1..5] of Integer; b: array[1..5] of Integer;
begin a[1] := 7; b := a; writeln(b[1]) end.
