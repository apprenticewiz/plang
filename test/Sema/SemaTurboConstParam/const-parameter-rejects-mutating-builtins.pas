(*
Issue #710.  A `const` parameter is refused ordinary assignment and
field-assignment (const-parameter-rejects-whole-and-field-assignment.pas,
this directory's sibling), but Turbo's own mutating builtins -- which take
their target as an "untyped" var-shaped argument rather than an ordinary
assignment -- never asked checkNotProtected about it: they checked isLValue
only.  For a structured `const` parameter (record/array/set), which CodeGen
passes BY REFERENCE (isStructuredForConstByRef, Sema/Type.h), that meant the
builtin reached straight through to the CALLER's own storage with no
diagnostic at all -- FillChar(r, SizeOf(r), 0) on a `const r: TPoint` zeroed
the caller's record.  A scalar/string const parameter is passed by value, so
the same call only ever mutated the callee's own copy -- caller-visible
corruption wasn't possible there, but the const contract still promises the
parameter is read-only inside the body (and `fpc -Mtp` refuses the identical
program), so the diagnostic belongs here too.

Ten calls below, one per mutating builtin named in the issue (Inc, Dec,
FillChar, Move, Include, Exclude, Delete, Insert, SetLength, Str), each
writing to one of the five const parameters.  Two builtins (Move, Insert)
also take a second, PLAIN argument that is only ever read -- Move's own
`src` and Insert's own literal source -- which must NOT be flagged; if the
fix over-eagerly checked every argument rather than only the write-target
one, this count would come out too high rather than too low.  The string
parameter is deliberately named `buf`, not the more obvious `str`: `str`
collides case-insensitively with the `Str` builtin itself, and shadowing it
would turn `Str(n, buf)` into "'str' is not callable" rather than exercising
the diagnostic this file is testing.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: grep -c "assignment to const parameter" %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:10
*)

program p;
type
  TPoint = record x, y: Integer end;

procedure Mutate(const r: TPoint; const src: TPoint; const s: set of Char;
                  const n: Integer; const buf: string);
begin
  FillChar(r, SizeOf(r), 0);
  Move(src, r, SizeOf(r));
  Include(s, 'z');
  Exclude(s, 'a');
  Inc(n);
  Dec(n);
  Delete(buf, 1, 1);
  Insert('x', buf, 1);
  SetLength(buf, 1);
  Str(n, buf)
end;

var v, w: TPoint; t: set of Char; k: Integer; u: string;
begin
  v.x := 1; v.y := 2; w.x := 3; w.y := 4;
  t := ['a']; k := 5; u := 'hello';
  Mutate(v, w, t, k, u)
end.
