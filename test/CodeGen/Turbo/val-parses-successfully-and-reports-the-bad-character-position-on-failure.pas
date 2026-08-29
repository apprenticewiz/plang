(*
System-unit string routines item, the critical Val verification: Val(s, v, code)
parses s into v and sets code to 0 on success, or the 1-based index of the
FIRST character that does not fit Val's grammar on failure.  Unlike every
other numeric-parsing entry point in this runtime (plang_read_i64/_f64 and
their _turbo siblings, all [[noreturn]]-fatal on malformed input via
plang_err_read_format/plang_err_read_int_range or plang_tp_runerror), Val
must NEVER abort the process on bad input -- control returns to the caller
either way.  This is proven here, not just asserted: 'after' prints AFTER a
Val call on genuinely malformed input ('12x'), which only happens if control
genuinely returned from the non-fatal parser (runtime/plang_val.cpp) rather
than std::exit-ing partway through.  Every value below was checked against a
local `fpc -Mtp` install; see plang_val_parse_int/_real's own doc comments
for the two DELIBERATE, documented divergences from fpc's exact behavior (an
unsigned destination's own rejection of a leading '-', and a trailing
'e'/'e+'/'e-' with no exponent digits) that this test does not exercise.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  v, code: integer;
  r: real;
begin
  writeln('before');

  Val('123', v, code);
  writeln(v, ' ', code);

  Val('12x', v, code);
  writeln(v, ' ', code);

  writeln('after');

  { A handful more of the empirically-derived cases, still on the same
    non-fatal path: leading blanks are skipped, a hex ($/x/X) prefix is
    accepted, trailing junk fails at its own position, and int64-overflow
    (not the destination's own narrower width) is what a huge literal
    fails at. }
  Val('  123', v, code);
  writeln(v, ' ', code);
  Val('$FF', v, code);
  writeln(v, ' ', code);
  Val('x12', v, code);
  writeln(v, ' ', code);
  Val('', v, code);
  writeln(v, ' ', code);
  Val('999999999999999999999', v, code);
  writeln(v, ' ', code);

  Val('3.14', r, code);
  writeln(r:0:2, ' ', code);
  Val('abc', r, code);
  writeln(r:0:2, ' ', code);
end.

(*
CHECK:before
CHECK-NEXT:123 0
CHECK-NEXT:0 3
CHECK-NEXT:after
CHECK-NEXT:123 0
CHECK-NEXT:255 0
CHECK-NEXT:18 0
CHECK-NEXT:0 1
CHECK-NEXT:0 19
CHECK-NEXT:3.14 0
CHECK-NEXT:0.00 1
*)
