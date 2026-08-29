(*
EP non-regression (Tier 2 capstone): 'write(s:w)' with a field-width
narrower than s's own length still TRUNCATES under Extended Pascal (ISO
10206's own write rule, unaffected by anything Turbo's write/writeln
coverage work touched -- Tier 1's "a field width is a minimum and never
truncates" is a documented TURBO-only reversal of this exact ISO/EP rule;
see test/Driver/Turbo/a-field-width-is-a-minimum-and-never-truncates.pas
and docs/turbo.md's own note on the four write-formatting reversals).  A
ten-character string field-written at width 3 still prints only its first
three characters here, under '-std=iso10206', exactly as it always has.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:Hel
CHECK-NEXT:[Hel]
*)

program p(output);
var
  s: string(10);
begin
  s := 'HelloWorld';
  writeln(s:3);
  writeln('[', s:3, ']')
end.
