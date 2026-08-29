(*
The other genuine-I/O-error case this item's own manual-testing requirement
names specifically: a permission-denied file must set InOutRes rather than
crash.  Uses the same portable technique this project's own
test/Module/SeparateCompilation/an-unwritable-pmi-directory-is-diagnosed-not-silently-skipped.pas
and test/Driver/Driver/dash-g-schema-sidecar-write-failure-is-a-nonzero-exit-not-silent-success.pas
already establish: chmod 000 on the CONTAINING directory (rather than the
file itself) is what actually makes fopen(3) fail with EACCES -- there is
no portable way to force a permission failure on the target file alone that
does not depend on who is running the test.  ParamStr(1) carries the locked
path in at runtime rather than embedding it as a literal, so the fixture
can live under lit's own %t rather than this test file's own source tree.

Maps to InOutRes 5 ("file access denied"), the same code EACCES/EROFS/
EEXIST/ENOTEMPTY/EBUSY/ENOTDIR/EISDIR all collapse to in the real `fpc`
3.2.2 table this item's plang_tp_posix_to_run_error matches (see that
function's own comment) -- not a code unique to EACCES alone.

RUN: rm -rf %t.dir && mkdir -p %t.dir/locked
RUN: printf 'secret\n' > %t.dir/locked/inner.txt
RUN: chmod 000 %t.dir/locked
RUN: %plang -std=turbo %s -o %t.exe
RUN: %run %t.exe %t.dir/locked/inner.txt | FileCheck %s
RUN: chmod 755 %t.dir/locked
*)

(*
CHECK:ioresult=5
CHECK-NEXT:did not crash
*)

var f: text;
begin
  assign(f, ParamStr(1));
  reset(f);
  writeln('ioresult=', IOResult);
  writeln('did not crash');
end.
