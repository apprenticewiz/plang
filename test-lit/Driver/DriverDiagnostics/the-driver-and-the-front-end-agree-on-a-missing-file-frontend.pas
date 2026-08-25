(*
One condition, one message: the driver reports the scanner's
err_file_not_found rather than keeping a copy of the wording.  The
prefix is the only difference, and clang -cc1 differs from clang the
same way.  This half checks the front end's message.
*)

(*
RUN: not %plang_ir -pc1 nosuchfile.pas > %t.out 2>&1
RUN: FileCheck --strict-whitespace --match-full-lines %s < %t.out
*)

(*
CHECK:error: no such file or directory: 'nosuchfile.pas'
*)
