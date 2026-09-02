(*
Issue #663: a genuine OS-level write failure (ENOSPC, here forced with the
portable /dev/full device, which always opens successfully but fails every
write) used to be misreported as InOutRes 105 ("file not open for output")
regardless of what actually went wrong -- tpTrapOnStreamError mapped ANY
ferror() on a write to that one fixed code without ever looking at errno,
even though plang_tp_posix_to_run_error already had ENOSPC -> 101 wired up
for every OTHER failure site in this file. Confirmed against `fpc -Mtp`:
both a text Write and a typed (`file of Byte`) Write against /dev/full
report 101, not 105. Exercises both writers, since each has its own
`_turbo`-suffixed entry point (plang_write_file_str_turbo/plang_write_binary_turbo)
and both used to share the same buggy tpTrapOnStreamError call.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck %s
*)

(*
REQUIRES: dev-full
*)

(*
CHECK:text write to /dev/full ioresult=101
CHECK-NEXT:typed write to /dev/full ioresult=101
*)

var
  f: Text;
  tf: file of Byte;
  r: Integer;
  i: Integer;
begin
  {$I-}
  assign(f, '/dev/full');
  rewrite(f);
  { A large enough write to overrun stdio's own output buffer and force a
    real write(2) syscall rather than sitting there merely buffered. }
  for i := 1 to 20000 do write(f, 'x');
  r := IOResult;
  writeln('text write to /dev/full ioresult=', r);

  assign(tf, '/dev/full');
  rewrite(tf);
  for i := 1 to 20000 do write(tf, Byte(65));
  r := IOResult;
  writeln('typed write to /dev/full ioresult=', r);
  {$I+}
end.
