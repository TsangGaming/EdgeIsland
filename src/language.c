#include "platform.h"
#include "language.h"

static const wchar_t language_value[] = L"Language";

int ui_language(void)
{
    HKEY key;
    DWORD value = UI_ENGLISH, size = sizeof value, type;
    LONG result;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return UI_ENGLISH;
    }
    result = RegQueryValueExW(key, language_value, NULL, &type, (BYTE *)&value, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof value && value == UI_SIMPLIFIED_CHINESE
           ? UI_SIMPLIFIED_CHINESE : UI_ENGLISH;
}

int save_ui_language(int language)
{
    HKEY key;
    DWORD value = (DWORD)language;
    LONG result;
    if (language != UI_ENGLISH && language != UI_SIMPLIFIED_CHINESE) {
        return 0;
    }
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return 0;
    }
    result = RegSetValueExW(key, language_value, 0, REG_DWORD, (const BYTE *)&value, sizeof value);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
