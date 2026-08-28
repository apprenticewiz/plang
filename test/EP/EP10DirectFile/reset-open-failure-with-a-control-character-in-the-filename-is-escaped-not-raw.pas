(*
Issue #420: plang_err_cannot_open (runtime/plang_sys.cpp) wrote a caller-
supplied filename straight to stderr with no control-character escaping --
the runtime-side twin of #281, which closed the identical terminal/log-
injection hole for the compiler's OWN diagnostics (a source filename from
argv, a locale tag, the -v/-### echo) via escapeControlChars (include/
plang/Basic/StringUtil.h).

A compiled Pascal program's own reset/rewrite/extend/update filename is a
Pascal string VALUE, not source text plang chose: a program can build it
however it likes, including with chr(27), and #281's escapeControlChars
never covered this path, because the runtime that ships with a compiled
program links with no C++ standard library at all (plang_sys.cpp's own
ModuleFinalisers comment) -- std::string is not available to it.

The repro is the issue's own: reset a nonexistent file whose name embeds
two ESC-introduced SGR escapes built via chr(27) at run time, hitting the
same open-failure path issue #150's own reset-open-failure test already
exercises (just with an innocuous name there). Before the fix,
plang_err_cannot_open wrote the two raw ESC bytes (0x1B) straight to
stderr -- confirmed with cat -A showing literal ^[ bytes, not the \x1b
text below. After the fix, every C0 byte (and DEL) is rendered as a
visible \xHH escape instead, the same threshold and escape form #281's
escapeControlChars uses -- so a terminal or a log tailing plang's stderr
sees six harmless ASCII bytes, never the control byte itself.

RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=OUT --allow-empty %s < %t.out
RUN: FileCheck %s < %t.err
*)

(*
OUT-NOT: should not reach here
*)

(*
CHECK: plang runtime: cannot open 'nofile\x1b[31mRED\x1b[0m' for reading
CHECK-NOT: 
*)

program reset_open_failure;
var f: file of integer;
begin
  reset(f, 'nofile' + chr(27) + '[31mRED' + chr(27) + '[0m');
  writeln('should not reach here')
end.
