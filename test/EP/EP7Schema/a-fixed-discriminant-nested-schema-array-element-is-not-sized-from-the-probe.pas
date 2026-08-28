(*
Issue #393.  `array[1..n] of inner(4)` -- an array whose ELEMENT is a schema
instantiated with a FIXED (literal, not discriminant-derived) discriminant --
inside a VARYING outer schema's own body crashed SchemaLayoutEngine's
rtSizeOfTypeNode with "a schema body denoter with no run-time layout".

Sibling to a-schema-instantiated-inside-a-schema-body-is-not-sized-from-the-
probe-matrix.pas, which covers the opposite shape: `vector(n)`, whose
discriminant reads the ENCLOSING schema's own and so genuinely needs a
closed form re-evaluated per instance.  `inner(4)`'s discriminant is a
compile-time constant, so Sema never builds a form for it -- there is
nothing in it that could vary between one occurrence and the next.  But
inner(4)'s own resolved body was still coming out stamped ExtentVaries=true:
it is resolved while OUTER's own probe pass is resolving OUTER's body, and
the probe machinery that marks an extent "varies" whenever it reads ANY
active discriminant binding does not turn itself off for the inner
instantiation's own body, even though `m` there is bound to the literal 4
and not to a probe stand-in.  rtSizeOfTypeNode used to trust that stamp
unconditionally and abort rather than ask the one question that actually
answers it: did Sema need a closed form for this instantiation at all.
*)

(*
RUN: %plang_ep -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 12 13 14
CHECK-NEXT:21 22 23 24
CHECK-NEXT:31 32 33 34
*)

program p(output);
type inner(m: integer) = array[1..m] of integer;
     outer(n: integer) = array[1..n] of inner(4);
var q: ^outer; i: integer;
begin new(q, 3);
  for i := 1 to 3 do begin
    q^[i][1] := i * 10 + 1; q^[i][2] := i * 10 + 2;
    q^[i][3] := i * 10 + 3; q^[i][4] := i * 10 + 4
  end;
  for i := 1 to 3 do
    writeln(q^[i][1]:1, ' ', q^[i][2]:1, ' ', q^[i][3]:1, ' ', q^[i][4]:1)
end.
