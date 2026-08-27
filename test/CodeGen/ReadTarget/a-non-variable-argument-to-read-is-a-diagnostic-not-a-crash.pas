(*
ISO §6.9.1: read(v) is defined as v := f^, so v must be a variable-access.
checkReadParamType validated the TYPE of a read argument but nothing ever
asked whether it was assignable at all, so a literal reached codegen with
no storage to read into and aborted the compiler instead of being refused
at the call site.
*)

(*
RUN: not %plang %s -o %t 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: read/readln target must be a variable
*)

program t(input, output);
begin read(5) end.
