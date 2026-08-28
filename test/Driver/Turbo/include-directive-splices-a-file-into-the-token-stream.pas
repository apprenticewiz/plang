(*
{$I file}/{$INCLUDE file} is Turbo's source-inclusion directive: the named
file's contents are spliced into the token stream right where the directive
stands, "as if" they had been written there directly, and scanning resumes
in the including file once the included one runs out (Directives.cpp's
openInclude/popInclude).  Three spellings of the same include, each its own
split-file scenario sharing one common.inc: the one-letter form, the long
form, and a single-quoted filename -- `fpc -Mtp` accepts all three, and so
does this.
*)

(*
RUN: split-file %s %t.dir

RUN: %plang -std=turbo %t.dir/short-form.pas -o %t.dir/a.bin
RUN: %run %t.dir/a.bin | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/long-form.pas -o %t.dir/b.bin
RUN: %run %t.dir/b.bin | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s

RUN: %plang -std=turbo %t.dir/quoted-form.pas -o %t.dir/c.bin
RUN: %run %t.dir/c.bin | FileCheck --check-prefix=RAN --strict-whitespace --match-full-lines %s
*)

(*
RAN:before
RAN:from the include
RAN:after
*)

//--- common.inc
  writeln('from the include');

//--- short-form.pas
program shortform;
begin
  writeln('before');
  {$I common.inc}
  writeln('after')
end.

//--- long-form.pas
program longform;
begin
  writeln('before');
  {$INCLUDE common.inc}
  writeln('after')
end.

//--- quoted-form.pas
program quotedform;
begin
  writeln('before');
  {$I 'common.inc'}
  writeln('after')
end.
