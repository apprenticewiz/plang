(*
Issue #586.  A substring range (`s[i..j]`) parses to its own AST node kind,
SubstringExpr (SubstringExpr::Str holding the underlying string), not
IndexExpr -- so Sema::protectedBaseOf's base-expression walk, which already
knew how to trace back through IndexExpr/FieldExpr/TypeCastExpr, stopped at
a SubstringExpr node and returned null, silently skipping the protected-
parameter check for exactly this one route.  `s[2..3] := 'XY'` reaches the
same underlying storage `s[2] := 'X'; s[3] := 'Y'` would, and read(f, s[i..j])
is EP section 6.9.1's read-is-an-assignment equivalent to the same write --
both must be refused the same way the single-index case already is.
*)

(*
RUN: not %plang_ep %s -o %t 2> %t.err
RUN: grep -c 'protected parameter' %t.err | FileCheck --check-prefix=COUNT --strict-whitespace --match-full-lines %s
*)

(*
COUNT:2
*)

program p(input, output);
var f: text;
procedure q(protected s: string(10));
begin
  s[2..3] := 'XY';
  readln(f, s[2..3])
end;
var t: string(10);
begin
  t := 'abcdef';
  q(t);
  writeln(t)
end.
