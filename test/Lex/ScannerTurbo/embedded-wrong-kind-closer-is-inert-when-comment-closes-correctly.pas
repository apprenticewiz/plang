(*
Confirmed against a real compiler (`fpc -Mtp`) before writing this test: a
comment opened with a brace and containing the OTHER dialect's closer
somewhere in its body, but closed for real with a matching close brace
before end of file, compiles clean under Turbo -- the stray closer is just
comment text, not a terminator.  err_comment_delim_mismatch (see the two
files right next door that DO trigger it) only fires when the comment
never gets a real matching close at all.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

program p;
{ this comment has a stray one right here: *) and keeps going regardless }
begin
  writeln(42)
end.

(*
CHECK: 42
*)
