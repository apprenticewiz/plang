(*
Tier 3 Cluster C item 5: the read half of the untyped-`file` fix -- see
write-on-an-untyped-file-is-refused-not-silently-textlike.pas (same
directory) for the write half and the full rationale.  Confirmed against
`fpc -Mtp`: `read(f, c)` for `var f: file;` is rejected with "Can't use read
or write on untyped file", not given some ad hoc byte-level meaning.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot use 'read' on an untyped file
*)

var f: file;
    c: char;
begin
  assign(f, 'read-on-an-untyped-file-is-refused.dat');
  rewrite(f);
  close(f);
  reset(f);
  read(f, c);
  close(f);
end.
