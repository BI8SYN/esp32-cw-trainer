#ifndef MORSE_TABLE_H
#define MORSE_TABLE_H

/** 返回字符的点划串（"." / "-"），未知字符返回 NULL。大小写不敏感。 */
const char *morse_table_lookup(char c);

/** 点划串 → 大写字符，未知返回 '\0'。 */
char morse_table_decode(const char *pattern);

#endif
