(*
RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

{ EP §6.7.6.2: re/im/arg are the one arithmetic-function family that is NOT
  also polymorphic over integer-type and real-type the way sqrt/sin/cos/exp/
  ln/arctan are (ISO 10206 Table 2's operand-type column: footnote (1),
  "integer-type, real-type, or complex-type", for those six; footnote (4),
  "Complex-type" alone, for these three) -- so a plain numeric argument is
  rejected here, not silently widened.  This had no check of any kind before
  (issue #306): `re(SomeInteger)` type-checked and CodeGen's non-complex
  fallback fabricated a plausible-looking but non-conforming result instead
  of a diagnostic.

  arg(r) below for a plain real r replaces the older
  rejects-a-real-zero-argument-to-arg-the-same-way.pas, which exercised
  issue #249's runtime trap for arg on a real zero -- that scenario is no
  longer reachable now that Sema refuses a non-complex argument to arg
  outright, at compile time, whatever its value.  arg(cmplx(0.0, 0.0))'s
  runtime trap (the origin has no defined phase angle) is a separate,
  still-live case: see rejects-the-origin-as-having-no-defined-phase-angle.pas. }

program p;
var
  i: integer;
  r: real;
  b: boolean;
begin
  writeln(re(i):1:1);
  writeln(im(b));
  writeln(arg(r))
end.

(*
ERR: 're' requires a complex argument, got 'integer'
ERR: 'im' requires a complex argument, got 'boolean'
ERR: 'arg' requires a complex argument, got 'real'
*)
