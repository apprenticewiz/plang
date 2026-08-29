(*
Regression gate, the exact contrast the Turbo PChar-arithmetic feature
above depends on: `q := p + 1` for a Char-pointee pointer is legal Turbo
(see test/CodeGen/Turbo/pchar-pointer-arithmetic-indexing-and-array-decay.pas
and a-users-own-char-pointer-type-gets-the-same-arithmetic-pchar-does.pas --
the rule applies to ANY ^Char, not only the name PChar), but ISO 7185
§6.7.2.5 gives a pointer only '=' and '<>' -- nothing else, arithmetic
included -- and Extended Pascal does not change that.  Sema::checkBinary's
pointer-arithmetic case (SemaExpr.cpp) is gated on Opts.turbo(), same as
every other Turbo-only operator overload in that function (see e.g. its
And/Or case), so the identical source has to stay a plain numeric-operand
type error under both of the other two dialects, not silently start
compiling as pointer arithmetic the way it would if the Turbo gate were
ever dropped or inverted -- exactly the hazard a structural ("pointee is
Char") rather than nominal ("is literally PChar") gate exists to guard
against, since ^char is completely ordinary and legal to declare in either
dialect.

RUN: not %plang -std=iso7185 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err

RUN: not %plang -std=iso10206 %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: operator '+' requires numeric operands, got '^char' and 'integer'
*)

program p;
type
  mycharptr = ^char;
var
  ptr, qtr: mycharptr;
begin
  new(ptr);
  qtr := ptr + 1;
end.
