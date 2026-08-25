(*
ActiveSchemaBindings_ is filled by an ordinary instantiation t(300) as
well as by the undiscriminated probe, so marking an extent as varying
whenever a binding was read marked every DISCRIMINATED instance's fields
too -- where the capacity is exactly known. That silently disabled
err_string_too_long and the subrange warning, and capped a string(300) at
the 255 that stands in for a capacity plang does not know. No
undiscriminated schema is involved: it was a regression on plain EP.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:281
*)

program p(output);
type t(m: integer) = record s: string(m) end;
var v: t(300); i: integer;
begin v.s := 'x';
      for i := 1 to 280 do v.s := v.s + 'y';
      writeln(length(v.s):1) end.
