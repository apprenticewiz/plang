(*
Issue #673: BlockRead/BlockWrite's own trailing 'amt' argument, and
GetMem's own 'p' argument, are OUT-parameters -- written by the call,
never read by it -- so a definite-assignment warning should not fire on
either of them even though neither has been given a value before this
call.  builtinAssigns (SemaFlow.cpp) named read/readln/new/readstr/
delete/setlength/insert/str/val but not blockread/blockwrite/getmem, so
each fell to the "only looks" default and warned "is read here before it
has been given a value" on a variable this very call is what gives one.
*)

(*
RUN: %plang -std=turbo -Wall %s -o %t 2> %t.err
RUN: %run %t
RUN: FileCheck --allow-empty --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR-ABSENT-NOT: is read here before it has been given a value
*)

program p;
var
  f: file of integer;
  buf: array[1..4] of integer;
  amt: integer;
  ptr: pointer;
begin
  assign(f, 'blockread_amt_test.dat');
  rewrite(f);
  buf[1] := 1; buf[2] := 2; buf[3] := 3; buf[4] := 4;
  blockwrite(f, buf, 4, amt);
  writeln(amt);
  close(f);
  getmem(ptr, 16);
  freemem(ptr, 16);
end.
