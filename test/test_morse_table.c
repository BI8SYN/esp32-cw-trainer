#include "test_util.h"
#include "morse_table.h"

TEST(lookup_letters_and_digits)
{
    ASSERT_EQ_STR(morse_table_lookup('K'), "-.-");
    ASSERT_EQ_STR(morse_table_lookup('m'), "--");
    ASSERT_EQ_STR(morse_table_lookup('5'), ".....");
    ASSERT_EQ_STR(morse_table_lookup('0'), "-----");
    ASSERT_EQ_STR(morse_table_lookup('?'), "..--..");
    ASSERT_EQ_STR(morse_table_lookup('='), "-...-");
    ASSERT_TRUE(morse_table_lookup('#') == NULL);
    ASSERT_TRUE(morse_table_lookup(' ') == NULL);
}

TEST(decode_roundtrip)
{
    const char *all = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,?/=+";
    for (const char *p = all; *p; p++) {
        const char *pat = morse_table_lookup(*p);
        ASSERT_TRUE(pat != NULL);
        ASSERT_EQ_INT(morse_table_decode(pat), *p);
    }
    ASSERT_EQ_INT(morse_table_decode("........"), 0);
    ASSERT_EQ_INT(morse_table_decode(""), 0);
}

int main(void)
{
    RUN(lookup_letters_and_digits);
    RUN(decode_roundtrip);
    TEST_MAIN_END();
}
