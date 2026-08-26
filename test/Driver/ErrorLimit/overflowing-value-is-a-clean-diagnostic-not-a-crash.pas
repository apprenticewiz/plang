(*
-ferror-limit=99999999999999999999 (well past what any integer type
holds) used to escape the argument parser as an uncaught
std::out_of_range from std::stoul, aborting the compiler instead of
reporting the bad flag.  It must get the same kind of clean diagnostic
and exit 1 as any other rejected -ferror-limit= value, with nothing
about a crash or an aborted process on stderr.  A negative value is
paired here too, since it exercises the same argument and must still
be rejected by the existing all-digits check, not accepted as huge via
unsigned wraparound.

RUN: not %plang -ferror-limit=99999999999999999999 %s -o %t 2> %t.overflow.err
RUN: FileCheck --check-prefix=OVERFLOW --strict-whitespace --match-full-lines %s < %t.overflow.err
RUN: not %plang -ferror-limit=-1 %s -o %t 2> %t.negative.err
RUN: FileCheck --check-prefix=NEGATIVE --strict-whitespace --match-full-lines %s < %t.negative.err
*)

(*
OVERFLOW:plang -pc1: -ferror-limit= value '99999999999999999999' is too large
NEGATIVE:plang -pc1: -ferror-limit= requires a number
*)

program p;
begin end.
