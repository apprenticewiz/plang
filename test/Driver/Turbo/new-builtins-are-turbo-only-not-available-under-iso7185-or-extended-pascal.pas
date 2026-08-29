(*
Regression gate, the same shape as
exitcode-is-turbo-only-not-available-under-iso7185-or-extended-pascal.pas:
SizeOf/High/Low/Hi/Lo/Swap/Include/Exclude/Inc/Dec/FillChar/Move are all
declared TP-only in Builtins.def (Dialects = TP), so each is a required
identifier under every dialect (Sema::registerBuiltins declares every
Builtins.def name everywhere) but refused BY NAME under -std=iso7185 and
-std=iso10206 via checkEPOnly's err_turbo_required_name, rather than
falling back to "undefined function/procedure" the way a name Builtins.def
never declared at all would.

The System-unit string routines (Copy/Pos/Concat/Delete/Insert/SetLength/
StringOfChar/UpCase/Str/Val) join this same list -- also TP-only in
Builtins.def.  Called here with plain integer arguments rather than any
ShortString ones: checkEPOnly's refusal fires before arity or argument-shape
checking ever runs (checkCallExpr/checkCallStmt's own ordering), so which
argument TYPES are given does not matter for this test.  Length is
DELIBERATELY NOT added here even though it is new to Turbo too: unlike
every name above, Length is EP|TP (EP already had it), so it is accepted
rather than refused under -std=iso10206 and does not fit this combined
"refused under both" gate -- see the CodeGen/Turbo suite for its own
Turbo-side coverage instead.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'sizeof' is a Turbo Pascal extension
CHECK: 'high' is a Turbo Pascal extension
CHECK: 'low' is a Turbo Pascal extension
CHECK: 'hi' is a Turbo Pascal extension
CHECK: 'lo' is a Turbo Pascal extension
CHECK: 'swap' is a Turbo Pascal extension
CHECK: 'include' is a Turbo Pascal extension
CHECK: 'exclude' is a Turbo Pascal extension
CHECK: 'inc' is a Turbo Pascal extension
CHECK: 'dec' is a Turbo Pascal extension
CHECK: 'fillchar' is a Turbo Pascal extension
CHECK: 'move' is a Turbo Pascal extension
CHECK: 'copy' is a Turbo Pascal extension
CHECK: 'pos' is a Turbo Pascal extension
CHECK: 'concat' is a Turbo Pascal extension
CHECK: 'delete' is a Turbo Pascal extension
CHECK: 'insert' is a Turbo Pascal extension
CHECK: 'setlength' is a Turbo Pascal extension
CHECK: 'stringofchar' is a Turbo Pascal extension
CHECK: 'upcase' is a Turbo Pascal extension
CHECK: 'str' is a Turbo Pascal extension
CHECK: 'val' is a Turbo Pascal extension
*)

program p;
type
  tcolor = (red, green, blue);
  tcolorset = set of tcolor;
var
  i, n, cnt: integer;
  s: tcolorset;
  buf: array [1 .. 4] of char;
begin
  n := sizeof(integer);
  n := high(integer);
  n := low(integer);
  n := hi(i);
  n := lo(i);
  n := swap(i);
  include(s, red);
  exclude(s, red);
  inc(i);
  dec(i);
  fillchar(buf, 1, ' ');
  move(buf, buf, 1);
  n := copy(i, i, i);
  n := pos(i, i);
  n := concat(i);
  delete(i, i, i);
  insert(i, i, i);
  setlength(i, i);
  n := stringofchar(i, i);
  n := upcase(i);
  str(i, i);
  val(i, i, i);
  cnt := n;
end.
