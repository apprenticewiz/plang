(*
Issue #592: reading a signed integer token that combines a leading '-' with
one of Turbo's radix prefixes ($/0x/&/%) -- e.g. "-$FF" -- used to fail to
parse under plang's Turbo read/readln, reporting InOutRes 106 ("invalid
numeric format") and setting the destination to 0, instead of correctly
parsing it as the negation of the magnitude (-$FF = -255) -- confirmed
against a local `fpc -Mtp` 3.2.2 install.  turboRadixPrefixFile
(runtime/plang_file.cpp) only ever recognized a prefix character at the very
front of the token, so a leading '-' before it made every one of its four
checks fail, falling through to decimal, where strtoll found no digits after
the sign and reported "entire token did not parse".  The plain, unprefixed
case ($FF alone, and a plain negative decimal, -5) already worked and must
keep working exactly as before.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:line1 "-$FF": i=-255 ioresult=0
CHECK-NEXT:line2 "$FF": i=255 ioresult=0
CHECK-NEXT:line3 "-5": i=-5 ioresult=0
CHECK-NEXT:line4 "-&17": i=-15 ioresult=0
*)

var
  f: text;
  i, r: Integer;
begin
  {$I-}
  assign(f, 'read-parses-a-negative-radix-prefixed-integer-literal.txt');
  rewrite(f);
  writeln(f, '-$FF');
  writeln(f, '$FF');
  writeln(f, '-5');
  writeln(f, '-&17');
  close(f);

  assign(f, 'read-parses-a-negative-radix-prefixed-integer-literal.txt');
  reset(f);

  read(f, i);
  r := IOResult;
  writeln('line1 "-$FF": i=', i, ' ioresult=', r);
  readln(f);

  read(f, i);
  r := IOResult;
  writeln('line2 "$FF": i=', i, ' ioresult=', r);
  readln(f);

  read(f, i);
  r := IOResult;
  writeln('line3 "-5": i=', i, ' ioresult=', r);
  readln(f);

  read(f, i);
  r := IOResult;
  writeln('line4 "-&17": i=', i, ' ioresult=', r);
  close(f);
end.
