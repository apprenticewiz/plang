(*
EP non-regression (Tier 2 capstone): 'string(1000)' -- EP's own unbounded-
relative-to-Turbo VarString capacity -- still compiles and holds a value
well past Turbo's own hard 255-character ShortString ceiling under
'-std=iso10206'.  Tier 2's whole reason for being is Turbo's `string[N]`
(ShortString), whose one-byte length field caps every declared capacity at
255 regardless of N (see docs/turbo.md's ShortString section); this pins
that plang's OTHER, older bounded-string type -- EP's own `string(N)`,
`TypeKind::VarString`, an entirely separate runtime
(runtime/plang_str.cpp) from Turbo's `plang_sstr.cpp` -- was never touched
by any of that work.  Built via concatenation, not direct indexed
assignment, since an EP VarString's own runtime LENGTH (not its capacity)
governs what index range is currently valid (ISO 10206 semantics, not
Turbo's fixed-size-buffer indexing) -- a fresh string(1000) starts at
length 0, same as it always has.
*)

(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:300
CHECK-NEXT:xx
*)

program p(output);
var
  s: string(1000);
  i: integer;
begin
  s := '';
  for i := 1 to 300 do s := s + 'x';
  writeln(length(s));
  writeln(s[1], s[300])
end.
