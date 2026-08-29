(*
Regression gate: ParseType.cpp's new 'procedure'/'function' type-denoter
case (parseTypeExpr) is gated to -std=turbo, the same way ISO 7185/Extended
Pascal keep every other Turbo-only denoter unreachable -- 'procedure'/
'function' starting a type expression outside a procedural PARAMETER's own
heading (ISO Sec6.6.3.1, parsed a different way entirely, by
parseProcedureParamGroup, and unaffected by this gate) has no ISO 7185 or
Extended Pascal meaning at all.  Falls through to the same
err_expected_type_expr every other unrecognized leading token gets.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: error: expected type expression, got 'procedure'
*)

program p;

type
  TProc = procedure(x: integer);

begin
end.
