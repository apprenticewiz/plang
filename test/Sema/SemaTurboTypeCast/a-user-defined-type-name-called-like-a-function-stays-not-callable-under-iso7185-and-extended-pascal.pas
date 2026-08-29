(*
Color(i) is a new, Turbo-only value typecast: parseFactor's identifier arm
(ParseExpr.cpp) only builds a TypeCastExpr for 'TypeName(expr)' when
Opts.turbo() holds, so under ISO 7185 or Extended Pascal 'Color(i)' still
parses exactly as it always has -- an ordinary CallExpr -- and Sema still
refuses it with the ordinary err_not_callable a type name used as a call
gets under those dialects (checkUserDefinedCall, SemaExpr.cpp), completely
unaware this feature exists.
*)

(*
RUN: not %plang -std=iso7185 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: 'Color' is not callable
*)

program p;
type
  Color = (Red, Green, Blue);
var
  i: integer;
  c: Color;
begin
  i := 1;
  c := Color(i);
end.
