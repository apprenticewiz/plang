(*
Tier 3 Cluster C item 5: the positive counterpart of
typed-file-write-requires-an-exact-type-match-not-just-assignment-compatibility.pas
(same directory) -- confirms the exact-type-identity rule does not reject a
genuinely matching Read/Write, round-tripping a `real` through a `file of
real` under -std=turbo.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
CHECK: 3.5000000000000000E+000
*)

var f: file of real;
    r: real;
begin
  assign(f, 'typed-file-write-with-a-matching-type-succeeds.dat');
  rewrite(f);
  r := 3.5;
  write(f, r);
  close(f);

  assign(f, 'typed-file-write-with-a-matching-type-succeeds.dat');
  reset(f);
  read(f, r);
  writeln(r);
  close(f);
end.
