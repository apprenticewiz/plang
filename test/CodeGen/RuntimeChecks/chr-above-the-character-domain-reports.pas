(*
RUN: %plang %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
*)

(*
ERR: value 256 out of range 0..255
*)

(*
issue #166: ISO §6.6.6.4 requires chr(x) to be an error when no character has
ordinal number x. CodeGen's chr case went straight to CreateTrunc with no
guard at all, so chr(256) silently truncated to i8 and came back as chr(0)
rather than being reported.
*)

program p;
begin writeln(ord(chr(256))) end.
