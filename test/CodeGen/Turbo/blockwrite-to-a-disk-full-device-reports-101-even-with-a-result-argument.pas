(*
Issue #665: BlockWrite used to suppress EVERY error once a result argument
was given -- the `!HasResult && Actual < Count` check was the ONLY place it
ever set InOutRes, so a genuine hard failure (here, /dev/full) silently
reported IOResult 0 with the caller none the wiser. Confirmed against
`fpc -Mtp`: a result argument only changes whether a SHORT-BUT-OTHERWISE-
SUCCESSFUL transfer is an error (the case BlockRead's own natural-EOF short
read exercises) -- a genuine write failure is reported via IOResult
regardless of arity, exactly like every other Turbo write entry point in
this file.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
REQUIRES: dev-full
*)

(*
CHECK:ioresult=101 amt=0
*)

var
  u: file;
  buf: array[0..4095] of Byte;
  amt: Integer;
begin
  {$I-}
  assign(u, '/dev/full');
  rewrite(u, 1);
  blockwrite(u, buf, 4096, amt);
  writeln('ioresult=', IOResult, ' amt=', amt);
  {$I+}
end.
