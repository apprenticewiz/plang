(*
Regression gate, the exact contrast the Turbo overload above depends on:
`5 and 3` is a legal bitwise expression under -std=turbo (see
and-or-are-bitwise-on-integer-operands-and-logical-on-boolean-ones.pas),
but 'and' has never been anything but the plain logical operator under
ISO 7185 or Extended Pascal, and Sema::checkBinary's And/Or arm only takes
the "both Integer" path when Opts.turbo() -- so the identical source has to
stay a TYPE ERROR (err_op_boolean, unchanged wording) under both of the
other two dialects, not silently start compiling as bitwise AND the way it
would if the Turbo gate were ever dropped or inverted.
*)

(*
RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: operator 'and' requires boolean operands, got 'integer' and 'integer'
*)

program p;
begin
  writeln(5 and 3)
end.
