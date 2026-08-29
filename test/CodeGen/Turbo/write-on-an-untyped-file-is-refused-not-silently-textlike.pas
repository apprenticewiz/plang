(*
Tier 3 Cluster C item 5: a genuinely untyped `file` (`var f: file;`, no `of`
clause) used to be silently treated exactly like `text` by both Sema and
CodeGen -- `read(f, c)` compiled and ran with zero diagnostic, silently
exercising the text/char-formatting code path against what is supposed to be
raw, untyped bytes.  Real Turbo Pascal has no plain Read/Write on an untyped
file at all -- only BlockRead/BlockWrite (a later Tier 3 item, not yet
implemented) can transfer its bytes -- confirmed against `fpc -Mtp`, which
rejects the identical program with "Can't use read or write on untyped
file".  This is the write half of that fix (see the sibling `read-...` test,
same directory, for the read half); both are now a clean compile-time
diagnostic under -std=turbo instead of an ad hoc, standard-mismatched
runtime behavior.

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: cannot use 'write' on an untyped file
*)

var f: file;
begin
  assign(f, 'read-write-on-an-untyped-file-are-refused-write.dat');
  rewrite(f);
  write(f, 'x');
  close(f);
end.
