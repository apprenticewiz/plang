(*
Tier 3 Cluster C item 5: ISO §6.9.1's `write(f,e)` is `f^ := e`, so its rule
is ordinary assignment compatibility -- an Integer value widens silently
into a `file of real`'s component, the same as `r := i` would for a `real`
variable `r`.  Real Turbo Pascal instead requires the written value's type
to be IDENTICAL to the file's component type: no implicit widening at all.
`fpc -Mtp` rejects the identical program with an incompatible-type error;
see typed-file-write-with-a-matching-type-succeeds.pas (same directory) for
the accepted, exact-match case.  ISO/EP's own widening for the identical
shape (write an Integer to a `file of real`) is unchanged -- confirmed by
hand against -std=iso7185, which still accepts it (Sema's isAssignCompatible
path, only skipped here under Opts.turbo()).

RUN: not %plang -std=turbo %s -o %t 2> %t.err
RUN: FileCheck %s < %t.err
*)

(*
CHECK: file element type is 'real', but value has type 'integer'
*)

var f: file of real;
    i: integer;
begin
  assign(f, 'typed-file-write-requires-an-exact-type-match.dat');
  rewrite(f);
  i := 7;
  write(f, i);
  close(f);
end.
