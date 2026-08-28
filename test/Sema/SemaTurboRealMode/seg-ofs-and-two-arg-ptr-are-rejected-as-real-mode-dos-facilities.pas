(*
Seg(x), Ofs(x) and the two-argument Ptr(seg, ofs) all require a parenthesized
argument list, so they parse as a CallExpr regardless of whether the name
means anything -- see ParseExpr.cpp's Identifier case, which builds a
CallExpr the instant it sees '(' and lets Sema sort out what the name is.
That routes them through checkCallExpr (SemaExpr.cpp), which normally raises
err_undefined_function for an unresolved name, not through checkIdent the
way a bare identifier or an indexed one (Mem[0]) is.  Confirms the
real-mode-DOS rejection is wired into checkCallExpr too, not just checkIdent
and checkCallStmt -- a plang program calling `Seg(x)` gets the specific
diagnostic, not "undefined function 'Seg'".

`Ptr` is deliberately two arguments here: this project declares no other
Ptr-named builtin of any arity (verified against Builtins.def), so there is
no legitimate single-argument or different-arity `Ptr` this rejection could
be shadowing.
*)

(*
RUN: not %plang -std=turbo -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK-DAG: 'Seg' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Ofs' is a real-mode DOS facility and has no meaning under -std=turbo on this target
CHECK-DAG: 'Ptr' is a real-mode DOS facility and has no meaning under -std=turbo on this target
*)

program p;
var x, y: integer;
begin
  x := Seg(y);
  x := Ofs(y);
  x := Ptr(1, 2)
end.
