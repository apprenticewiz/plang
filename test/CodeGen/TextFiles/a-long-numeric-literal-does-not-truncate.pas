(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:li=1 fi=1 lr=OK fr=OK
*)

(* issue #237: the token buffer both readstr/read-on-stdin (plang_io.cpp's
   scanNumber) and read-on-a-text-file (plang_file.cpp's fscanf, replaced by
   its own scanNumberFile as part of this fix) built a literal into used to
   be a fixed 64 characters, silently dropping everything past the limit --
   readstr and a text-file read on the very same bytes disagreed (the former
   truncated, the latter went through fscanf's own unlimited "%lld"/"%lf"),
   and a long real lost its exponent along with the truncated digits (a
   71-digit literal for 1e70 used to read back as roughly 1e62).
   li/fi below is a 64-character run of digits, one short of maxint's own
   19 digits many times over, that reads as 1 only if every digit including
   the final one is kept; lr/fr is a 71-digit literal for 1e70, checked with
   a wide tolerance since it is not exactly representable in a double and
   only needs to land in the right order of magnitude to prove the exponent
   was not truncated away. *)
program p;
var
  f: text;
  li, fi: integer;
  lr, fr: real;
begin
  readstr('0000000000000000000000000000000000000000000000000000000000000001', li);
  readstr('10000000000000000000000000000000000000000000000000000000000000000000000', lr);
  rewrite(f);
  writeln(f, '0000000000000000000000000000000000000000000000000000000000000001');
  writeln(f, '10000000000000000000000000000000000000000000000000000000000000000000000');
  reset(f);
  read(f, fi);
  read(f, fr);
  write('li=', li, ' fi=', fi);
  if (lr > 0.99e70) and (lr < 1.01e70) then write(' lr=OK') else write(' lr=FAIL');
  if (fr > 0.99e70) and (fr < 1.01e70) then writeln(' fr=OK') else writeln(' fr=FAIL')
end.
