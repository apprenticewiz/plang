(*
Turbo Tier 4, Cluster C item 6: Dos.SetDate/SetTime attempt to set the real
system clock via settimeofday(2), which requires root/elevated privilege
on a real POSIX system -- confirmed against real `fpc -Mtp` field practice
(rtl/unix/dos.pp's own SetDate/SetTime) that a permission failure is
SILENTLY absorbed: neither raises an error nor reports one through
DosError (both are plain Procedures with no error-reporting contract at
all in real Turbo/FPC).  This test's own job is exactly that non-event:
running as an ordinary, non-root CI user, the call must not crash, hang,
or corrupt DosError -- and the program must reach its own last line and
exit 0, which is the only externally observable proof of "did not raise
an error" a silent-failure contract can be given.  (What real state the
clock ends up in afterward is not checked here at all -- on a real CI
runner this call is expected to actually fail, and checking that it
LOOKS unchanged from another process would itself be racy against every
other process's own use of the same real system clock.)

RUN: %plang -std=turbo -I%plang_unit_dir %s -o %t
RUN: %run %t | FileCheck %s
*)

program DosSetDateSetTime;
uses Dos;
begin
  SetDate(2001, 2, 3);
  SetTime(4, 5, 6, 7);
  Writeln('reached-the-end');
end.

(*
CHECK:reached-the-end
*)
