(*
A structured typed-constant initializer -- a record or an array -- folds to
a compile-time llvm::Constant aggregate (buildTypedConstInit,
CGTypedConst.cpp) rather than the runtime store/GEP/memcpy sequence EP's own
structured-value constructors use (CGStructuredValue.cpp), which only ever
fill in storage that already exists.  Turbo's own literal syntax is `(...)`,
unlike EP's labeled `[...]`: a record constructor is written 'field: value'
per member (parseTurboConstValue, ParseDecl.cpp), and an array constructor is
purely positional, in declaration order, with no index label at all.  Also
exercises a record field itself initialized from an array typed constant's
own kind of literal, one level of nesting deep.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20
CHECK-NEXT:1 2 3
CHECK-NEXT:100 200 300
*)

type
  TPoint = record
    X, Y: Integer;
  end;
  TArr = array[1..3] of Integer;
  TArrOfArr = array[1..1] of TArr;

const
  P: TPoint = (X: 10; Y: 20);
  A: TArr = (1, 2, 3);
  Nested: TArrOfArr = ((100, 200, 300));

begin
  writeln(P.X, ' ', P.Y);
  writeln(A[1], ' ', A[2], ' ', A[3]);
  writeln(Nested[1][1], ' ', Nested[1][2], ' ', Nested[1][3]);
end.
