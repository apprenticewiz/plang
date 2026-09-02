(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:valid
*)

(* issue #774: the enumeration's constants (rfValid, rfCalibrated,
   rfSuspect) are anonymous -- named only inline as the base type of a set
   -- and used to compile but fail at link time with an undefined symbol
   for each one, because registerEnumValues() didn't recurse into a
   SetTypeNode's base type the way it did an array's element/index type. *)
program AnonEnumSet(output);
type
  TRecord = record
    Flags: set of (rfValid, rfCalibrated, rfSuspect);
  end;
var r: TRecord;
begin
  r.Flags := [rfValid, rfSuspect];
  if rfValid in r.Flags then writeln('valid')
end.
