(*
One condition, one message: the driver reports the scanner's
err_file_not_found rather than keeping a copy of the wording.  The
prefix is the only difference, and clang -cc1 differs from clang the
same way.  This half checks the driver's message.
*)

(*
RUN: not %plang_ir nosuchfile.pas > %t.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:plang: error: no such file or directory: 'nosuchfile.pas'
*)
