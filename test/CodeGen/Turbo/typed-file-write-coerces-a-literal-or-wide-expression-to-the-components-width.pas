(*
Issue #683: `write(f, e)` on a `file of Integer` under -std=turbo used to
require e's LLVM representation to already be exactly the component's
declared width, on the assumption that Sema's own
err_turbo_typed_file_exact_type check (SemaStmt.cpp) already guaranteed it.
That is true of the LOGICAL Pascal type e resolves to, but not of what
CodeGen actually emits for it: an IntLitExpr always lowers to an i64
constant regardless of context (CGExprCore.cpp), and a binary expression
mixing two different operand widths widens to the WIDER one
(CGBinaryOps.cpp) -- so, against Turbo's 16-bit Integer, both a bare
literal (`write(f, 100)`) and a wide expression (`write(f, v + 0)`) reached
BuiltinIO.cpp's width-equality assertion and aborted the compile ("a value
written to a typed file is not the width of the file's component"), even
though the identical `write(f, v)` on a plain variable worked fine.
BuiltinIO.cpp's typed-file write now always runs its CoerceToType width
fixup, not only for ISO/EP -- safe under Turbo too, since Sema's own
exact-type check already guarantees e and the file's component share the
same declared Pascal type, so the fixup can only ever be a same-kind
zext/sext/trunc here, never the cross-kind (int-to-real) promotion Turbo
disallows.
*)

(*
RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
CHECK-NEXT:100
CHECK-NEXT:7
*)

var f: file of integer;
    v: integer;
begin
  assign(f, 'typed-file-write-coerces-a-literal-or-wide-expression.dat');
  rewrite(f);
  v := 42;
  write(f, v + 0);   { a wide (i64) binary expression }
  write(f, 100);     { a bare integer literal, always i64 in CodeGen }
  v := 3;
  write(f, v + 4);   { a wide expression over two non-literal operands }
  close(f);

  assign(f, 'typed-file-write-coerces-a-literal-or-wide-expression.dat');
  reset(f);
  read(f, v); writeln(v);
  read(f, v); writeln(v);
  read(f, v); writeln(v);
  close(f);
end.
