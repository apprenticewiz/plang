(*
Turbo string[N]'s prior landing only implemented write/read to and from
stdin/stdout (plang_sstr_write/plang_sstr_read, plang_sstr.cpp): a file-
variable destination reached an explicit codegenICE ("ShortString file I/O
is not implemented yet"), an outright compiler crash rather than a
diagnostic or working support, for the same write(f, s)/read(f, s) shapes
every other string kind (VarString, a fixed-string-type) already supports.
This exercises the PascalFile-aware plang_sstr_write_file(_w)/
plang_sstr_read_file entry points (plang_file.cpp) that back it now: a
field-width write to a named file, and a readln back out of it.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[      hi]
*)

var
  f: Text;
  s, t: string[10];
begin
  s := 'hi';
  rewrite(f, '/tmp/plang_shortstring_file_regtest.txt');
  write(f, s:8);
  writeln(f);
  close(f);

  reset(f, '/tmp/plang_shortstring_file_regtest.txt');
  readln(f, t);
  writeln('[', t, ']');
  close(f);
end.
