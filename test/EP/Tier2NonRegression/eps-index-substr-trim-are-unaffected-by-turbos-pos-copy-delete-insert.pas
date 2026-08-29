(*
EP non-regression (Tier 2 capstone): EP's own `index`/`substr`/`trim`
builtins are completely unaffected by anything Turbo's `Pos`/`Copy`/
`Delete`/`Insert` added, even though several of those Turbo routines are
DELIBERATELY the opposite of their EP near-namesakes on the exact question
that matters (docs/turbo.md's "The System-unit string routines" table):
`Pos('', s)` is 0, but EP's `index('', s)` is still 1 (ISO 10206's own
rule) -- checked here first, and unchanged.  `substr` still RAISES on an
out-of-range request (Copy instead clamps -- not exercised for the error
case here, since that already has its own dedicated coverage in
test/EP/StringCapacity/ and this file's purpose is the HAPPY-path answers
staying the same, not the divergence itself, which docs/turbo.md already
covers in full).  `trim` still applies ISO 10206's own trailing-blanks-only
rule (leading blanks are untouched), not some Turbo-flavored two-sided
trim -- and indeed Turbo has no `Trim` at all (deliberately excluded, a
later SysUtils-era addition -- see docs/turbo.md's string-routines table).
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:1
CHECK-NEXT:Hello
CHECK-NEXT:[  padded]
*)

program p(output);
var
  s: string(20);
begin
  s := 'Hello World';
  writeln(index(s, 'World'));
  writeln(index(s, ''));
  writeln(substr(s, 1, 5));
  writeln('[', trim('  padded  '), ']')
end.
