// [utest->req~auth-005~1]
// Covers the pure helpers extracted from the password / profile dialogs
// (REQ-AUTH-005 avatar MIME gating + the shared new-password rule used
// by ResetPasswordDialog and ProfileDialog). Both were private dialog
// logic; pulling them into headers lets them test without GTK.

#include "src/gtk/view/dialogs/PasswordValidation.h"
#include "src/gtk/view/dialogs/AvatarMime.h"

#include <gtest/gtest.h>

namespace {

using app::view::avatarmime::mimeFromPath;
using app::view::pwvalidation::PasswordError;
using app::view::pwvalidation::validateNewPassword;

// --- password validation (shared by reset + profile dialogs) --------

TEST(DialogHelpersTest, EmptyNewPasswordIsRejectedBeforeMismatch) {
    // Blank reports the actionable "required", even if confirm differs.
    EXPECT_EQ(validateNewPassword("", "anything"), PasswordError::Empty);
    EXPECT_EQ(validateNewPassword("", ""), PasswordError::Empty);
}

TEST(DialogHelpersTest, MismatchedPasswordsRejected) {
    EXPECT_EQ(validateNewPassword("secret", "different"),
              PasswordError::Mismatch);
}

TEST(DialogHelpersTest, MatchingNonEmptyPasswordAccepted) {
    EXPECT_EQ(validateNewPassword("secret", "secret"), PasswordError::None);
}

// --- avatar MIME gating (profile dialog upload) ----------------------

TEST(DialogHelpersTest, MimeFromPathAcceptsPngAndJpeg) {
    EXPECT_EQ(mimeFromPath("photo.png"), "image/png");
    EXPECT_EQ(mimeFromPath("photo.jpg"), "image/jpeg");
    EXPECT_EQ(mimeFromPath("photo.jpeg"), "image/jpeg");
}

TEST(DialogHelpersTest, MimeFromPathIsCaseInsensitive) {
    EXPECT_EQ(mimeFromPath("/home/op/Avatar.PNG"), "image/png");
    EXPECT_EQ(mimeFromPath("C:/x/IMG.JPG"), "image/jpeg");
}

TEST(DialogHelpersTest, MimeFromPathRejectsUnsupportedOrExtensionless) {
    EXPECT_EQ(mimeFromPath("doc.pdf"), "");
    EXPECT_EQ(mimeFromPath("script.sh"), "");
    EXPECT_EQ(mimeFromPath("noextension"), "");
    EXPECT_EQ(mimeFromPath(""), "");
}

}  // namespace
