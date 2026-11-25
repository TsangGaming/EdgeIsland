#ifndef EDGE_LANGUAGE_H
#define EDGE_LANGUAGE_H
#include <stddef.h>

enum UiLanguage { UI_ENGLISH, UI_SIMPLIFIED_CHINESE };

static inline const wchar_t *ui_text(int language, const wchar_t *english, const wchar_t *chinese)
{
    return language == UI_SIMPLIFIED_CHINESE ? chinese : english;
}

int ui_language(void);
int save_ui_language(int language);
#endif
