(*
Issue #714.  readstr(e, v1,...,vn) / writestr(e, ...) assign straight into
their target(s) the same way read/readln's targets do, but their own arm in
Sema::checkCallStmt (SemaStmt.cpp) only ever checked isLValue before this
fix -- it never called checkNotProtected, the guard read/readln's own
targets already got after issue #586.  A `protected` string parameter's
contents were silently mutable through writestr even though a direct
`s := 'hi'` assignment on the same parameter was already refused.

Four calls below: writestr's own destination (arg 0) is checked twice, once
directly (`writestr(s, 'hi')`) and once via a field path (`writestr(r.s,
'hi')`) the same way #710/#711 checked both paths for other builtins;
readstr's source (arg 0, read-only, must NOT be flagged) and its actual
write-target (arg 1, `v`, an ordinary non-protected var -- also must NOT be
flagged) round out the file to show the check is precise about which
argument index is the target for each builtin.

RUN: not %plang_ep %s -o %t 2> %t.err
RUN: grep -c 'protected parameter' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:2
*)

program p(output);
type rec = record s: string(20) end;
procedure q(protected var s: string(20); protected var r: rec; var v: integer);
begin
  writestr(s, 'hi');
  writestr(r.s, 'hi');
  readstr(s, v)
end;
var t: string(20); g: rec; n: integer;
begin
  t := 'xx'; g.s := 'yy'; n := 0;
  q(t, g, n);
  writeln(t)
end.
