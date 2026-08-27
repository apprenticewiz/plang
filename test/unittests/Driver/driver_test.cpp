// driver_test.cpp -- in-process regression test for plang::Driver's own
// C++ API contract (issue #174).
//
// Driver.h documents run() as returning a Unix exit code to its caller, but
// parseArgs used to call std::exit() directly for --version, --help,
// -dumpversion, -dumpmachine, and --help-warnings.  Running plang as a
// subprocess -- what every test/Driver/ lit case does, and the only way
// this project's own CLI is ever actually invoked -- cannot tell that apart
// from a normal return: the exit code and everything printed are identical
// either way, since std::exit() reports the same code std::exit()'s caller
// would have returned anyway. The difference only matters to a caller that
// links PlangDriver and calls Driver::run() in-process instead of spawning
// plang -- std::exit() there tears down that host's whole process out from
// under it rather than returning control the way Driver.h promises. With no
// CLI-observable proxy for that distinction, this can only be tested
// in-process -- the same reasoning test/unittests/Basic/catalog_test.cpp's
// own header comment gives for its permanent GoogleTest cases, and exactly
// what issue #174's own suggested direction asked for ("add in-process
// tests").
//
// EXPECT_EXIT is doing double duty here, not just its usual "does this
// terminate the process" job. runInChildAndReport()'s statement always
// finishes by calling std::exit() itself, with a marker already written to
// stderr -- so the *only* way it can fail to reach that marker is if
// Driver::run() exited the child first. A plain (non-death) EXPECT_EQ on
// run()'s return value cannot make this distinction on its own: a child
// that never came back from run() because parseArgs called std::exit(0)
// partway through it, and a child that returned normally with rc == 0,
// both look like "exited with code 0" to whatever ran them -- one of them
// just never reached the assertion that would have said so.

#include "plang/Driver/Driver.h"

#include "gtest/gtest.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

/// Runs \p Args through Driver::run() and, only if that call returns
/// control normally, prints a marker naming the return code and exits with
/// it. Meant to run inside EXPECT_EXIT's forked child: if Driver::run()
/// instead calls std::exit() itself (the bug), the child is gone before the
/// marker line is ever reached.
void runInChildAndReport(std::vector<std::string> Args) {
    std::vector<char *> Argv;
    Argv.push_back(const_cast<char *>("plang"));
    for (std::string &A : Args) Argv.push_back(A.data());

    testing::internal::CaptureStdout(); // keep --version/--help's own banner
                                         // out of the test log; irrelevant here
    const int Rc = plang::Driver(nullptr).run(static_cast<int>(Argv.size()), Argv.data());
    testing::internal::GetCapturedStdout();

    std::fprintf(stderr, "DRIVER-RUN-RETURNED rc=%d\n", Rc);
    std::exit(Rc);
}

void expectRunReturnsZero(std::vector<std::string> Args) {
    EXPECT_EXIT(runInChildAndReport(std::move(Args)),
                ::testing::ExitedWithCode(0),
                "DRIVER-RUN-RETURNED rc=0");
}

} // namespace

TEST(DriverInProcessDeathTest, VersionReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"--version"});
}

TEST(DriverInProcessDeathTest, DumpversionReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"-dumpversion"});
}

TEST(DriverInProcessDeathTest, DumpmachineReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"-dumpmachine"});
}

TEST(DriverInProcessDeathTest, HelpReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"--help"});
}

TEST(DriverInProcessDeathTest, HelpWarningsReturnsControlInsteadOfExiting) {
    expectRunReturnsZero({"--help-warnings"});
}

// run()'s other, non-early-exit return path (compile() has always returned
// an ordinary int) was never in question, but it is cheap to confirm the
// informational-action fix did not somehow change it too: no arguments at
// all is diag::err_no_input_files, rc == 1, and always was a normal return
// rather than a std::exit() -- included as a control alongside the five
// informational-action cases above, all of which the fix changed from
// std::exit() to a normal return with the same rc == 0 they always had.
TEST(DriverInProcessDeathTest, NoInputFilesReturnsOneInsteadOfExiting) {
    EXPECT_EXIT(runInChildAndReport({}),
                ::testing::ExitedWithCode(1),
                "DRIVER-RUN-RETURNED rc=1");
}
