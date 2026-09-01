// options_table_test.cpp -- guards against Options.def/-pc1 parser drift
// (issue #181).
//
// Options.def is the shared catalog the driver reads to recognize an option,
// build both --help texts, and decide whether to forward an option to the
// front end.  It is NOT read by frontendPC1Main() (Frontend.cpp): that
// function has its own hand-written argument loop, so a Frontend/Both entry
// added to the table still needs its own arm added there by hand, or -pc1
// will reject it as an unrecognized argument -- see Options.def's own header
// comment, and the -dump-vmt gap this issue was filed over (present in the
// table's driver-side/help-text uses but absent from -pc1's own parser until
// this same change added it).
//
// This test cannot make frontendPC1Main's parser table-driven -- most
// options need real per-option logic (-std= validation, numeric parsing, -o
// handling) that a value-only table can't drive -- so instead it is a
// completeness *test*: for every table entry marked Frontend or Both, it
// synthesizes a minimal -pc1 argv naming that option and asserts
// frontendPC1Main's parser does not fall through to
// diag::warn_unrecognized_argument ("unrecognized argument '...'") for it.
// A future option added to the table but never taught to -pc1's own parser
// fails this loudly in CI instead of shipping silently, which is the actual
// risk issue #181 is about.

#include "plang/Frontend/Frontend.h"
#include "plang/Driver/Options.h"

#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <vector>

using namespace plang;

namespace {

/// Builds the -pc1 argv that exercises \p O: just its spelling for a Flag,
/// spelling+value glued on for Joined/JoinedOrSeparate (the joined form,
/// since that's a single argv entry regardless of which form the option
/// supports), or spelling then value as two entries for Separate.  The value
/// itself does not need to be meaningful -- a badly-formed one is free to
/// make -pc1 report a different, specific diagnostic (an unknown dialect, an
/// unparsable number) -- it just must not be "unrecognized argument", which
/// is the one outcome this test rules out.
std::vector<std::string> argvFor(const opts::Option& O) {
    switch (O.OptKind) {
    case opts::Kind::Flag:
        return {std::string(O.Spelling)};
    case opts::Kind::Joined:
    case opts::Kind::JoinedOrSeparate:
        return {std::string(O.Spelling) + "x"};
    case opts::Kind::Separate:
        return {std::string(O.Spelling), "x"};
    }
    return {std::string(O.Spelling)};
}

/// Runs frontendPC1Main on Args (with argv[0]/[1] filled in as "plang"/
/// "-pc1", and -fdiagnostics-language=en forced first so the diagnostic text
/// this test greps for does not depend on the host's locale environment) and
/// returns everything it printed to stdout and stderr combined.
std::string runFrontendCapturingOutput(const std::vector<std::string>& Args) {
    std::vector<std::string> Full = {"plang", "-pc1", "-fdiagnostics-language=en"};
    Full.insert(Full.end(), Args.begin(), Args.end());

    std::vector<char*> Argv;
    Argv.reserve(Full.size());
    for (std::string& A : Full) Argv.push_back(A.data());

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    (void)frontendPC1Main(static_cast<int>(Argv.size()), Argv.data());
    const std::string Out = testing::internal::GetCapturedStdout();
    const std::string Err = testing::internal::GetCapturedStderr();
    return Out + Err;
}

class OptionsTableCompleteness : public ::testing::TestWithParam<opts::Option> {};

TEST_P(OptionsTableCompleteness, PC1ParserRecognizesEveryFrontendOrBothOption) {
    const opts::Option& O = GetParam();
    const std::vector<std::string> Args = argvFor(O);
    const std::string Output = runFrontendCapturingOutput(Args);

    EXPECT_EQ(Output.find("unrecognized argument"), std::string::npos)
        << "Options.def lists '" << O.Spelling << "' as reaching -pc1 "
        << "(Consumer::Frontend or Consumer::Both), but frontendPC1Main's "
        << "own argument loop in Frontend.cpp has no arm for it -- add one, "
        << "matching the existing -dump-ast/-dump-tokens/-dump-parse-tree/"
        << "-dump-vmt pattern.  Captured output:\n"
        << Output;
}

/// Every table entry the driver would forward to -pc1 (Frontend or Both),
/// skipping only "-h"/"--help" (Display/Help both empty for one of the two
/// spellings sharing that text -- see Options.h's helpText, which uses that
/// emptiness to hide an alias; not relevant here, both are still exercised
/// once each since Spelling differs) -- nothing is skipped that would
/// actually change what this test proves.
std::vector<opts::Option> frontendOrBothOptions() {
    std::vector<opts::Option> Result;
    for (const opts::Option& O : opts::Table)
        if (opts::goesToFrontend(O)) Result.push_back(O);
    return Result;
}

INSTANTIATE_TEST_SUITE_P(
    OptionsDef, OptionsTableCompleteness,
    ::testing::ValuesIn(frontendOrBothOptions()),
    [](const ::testing::TestParamInfo<opts::Option>& Info) {
        // A readable-in-CI-output test name: strip everything gtest's naming
        // rules do not allow (identifier characters only) so e.g. "-std="
        // becomes "std" and "--target=" becomes "target", rather than the
        // whole run failing to instantiate over an illegal name.
        std::string Name;
        for (char C : Info.param.Spelling)
            if (std::isalnum(static_cast<unsigned char>(C))) Name += C;
        if (Name.empty()) Name = "opt";
        return Name + "_" + std::to_string(Info.index);
    });

} // namespace
