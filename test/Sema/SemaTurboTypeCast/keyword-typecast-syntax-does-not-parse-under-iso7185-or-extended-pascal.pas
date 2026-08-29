(*
Integer(x), Real(x), Boolean(x), Char(x) and String(x) are new, Turbo-only
syntax (parseFactor's and parseStatement's keyword-token cast arms,
ParseExpr.cpp/ParseStmt.cpp, both gated on Opts.turbo()).  ISO 7185 and
Extended Pascal have no typecast syntax at all, so a bare occurrence of one
of these type-name keywords in expression position must still be the
ordinary "expected expression" parse error it always was -- not silently
accepted, and not some new, different diagnostic.
*)

(*
RUN: not %plang -std=iso7185 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 -dump-ast %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: expected expression, got 'integer'
*)

program p;
var
  r: real;
  i: integer;
begin
  i := integer(r);
end.
