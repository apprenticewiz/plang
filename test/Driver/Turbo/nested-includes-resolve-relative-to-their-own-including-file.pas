(*
Two things at once: nested includes genuinely nest (main includes sub/b.inc,
which itself includes c.inc, three files deep), and resolveIncludePath
resolves a bare filename against THIS scanner's currently active buffer's
own directory -- sub/b.inc's, not main.pas's -- so sub/c.inc is found even
though there is no c.inc anywhere next to main.pas itself.  If resolution
were (wrongly) always relative to the outermost file, this would fail to
find c.inc at all and report err_directive_include_not_found instead of
running.
*)

(*
RUN: split-file %s %t.dir
RUN: %plang -std=turbo %t.dir/main.pas -o %t.dir/prog
RUN: %run %t.dir/prog | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:main before
CHECK:b before
CHECK:from c
CHECK:b after
CHECK:main after
*)

//--- sub/c.inc
  writeln('from c');

//--- sub/b.inc
  writeln('b before');
  {$I c.inc}
  writeln('b after');

//--- main.pas
program nested;
begin
  writeln('main before');
  {$I sub/b.inc}
  writeln('main after')
end.
