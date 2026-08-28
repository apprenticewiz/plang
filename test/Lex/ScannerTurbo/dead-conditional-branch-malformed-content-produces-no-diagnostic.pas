(*
The requirement skipToNextConditionalMarker exists to satisfy (see its own
comment in Scanner.h): source inside a branch that was never taken is
never tokenized, so a genuine syntax error in it is never diagnosed
either -- not "diagnosed and then discarded", not diagnosed at all. The
dead branch below has an unterminated string literal (would be
err_unterminated_string if this were ever scanned live), a comment opener
with no closer, and a run of characters no dialect accepts outside a
string or comment -- three different ways a naive implementation that
still lexes-and-discards dead content, rather than truly skipping the raw
text, would produce a diagnostic here. --allow-empty plus CHECK-NOT for
every severity word this compiler ever prints is the whole assertion: no
output at all is success, any diagnostic at all is failure.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s > %t.out 2>&1
RUN: FileCheck --allow-empty %s < %t.out
*)

(*
CHECK-NOT: note:
CHECK-NOT: warning:
CHECK-NOT: error:
*)

program p;
begin
  {$IFDEF NEVER}
  var Broken = 'this string never closes
  and has an unterminated { comment too, plus a run of $$$ garbage @@@ !!! ???
  {$ENDIF}
  writeln('ok')
end.
