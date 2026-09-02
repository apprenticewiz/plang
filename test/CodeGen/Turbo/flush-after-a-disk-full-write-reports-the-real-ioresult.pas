(*
Issue #664: Flush(f) used to ignore fflush(3)'s own return value entirely,
so IOResult stayed 0 no matter what actually happened. A SMALL write (well
under stdio's own output buffer) to /dev/full is deliberately used here,
not a large one: the write itself succeeds at the C level (the failing
write(2) does not happen until the buffer is actually flushed), so this
specifically exercises Flush's OWN error check, not Write's -- confirmed
against `fpc -Mtp`: IOResult stays 0 right after the small Write and only
becomes 101 ("disk write error") after the following Flush.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
REQUIRES: dev-full
*)

(*
CHECK:after small write ioresult=0
CHECK-NEXT:after flush ioresult=101
*)

var
  f: Text;
  r: Integer;
begin
  {$I-}
  assign(f, '/dev/full');
  rewrite(f);
  write(f, 'x');
  r := IOResult;
  writeln('after small write ioresult=', r);
  flush(f);
  r := IOResult;
  writeln('after flush ioresult=', r);
  {$I+}
end.
