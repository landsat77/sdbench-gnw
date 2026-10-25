/*
 * i18n for host builds (no firmware ABI / VTOR): English, or the language
 * in GW_LANG (for example GW_LANG=fr).
 */
#include "gw_core_i18n.h"

#include <stdlib.h>
#include <string.h>

static int is_english(const char *lang)
{
    return lang && (strcmp(lang, "en") == 0 || strcmp(lang, "en_us") == 0);
}

const char *gw_i18n(const gw_i18n_entry_t *entries)
{
    const char *en = NULL;
    const gw_i18n_entry_t *e;

    if (!entries)
        return "";
    const char *want = getenv("GW_LANG");
    if (want)
        for (e = entries; e->lang != NULL; e++)
            if (strcmp(e->lang, want) == 0 && e->text)
                return e->text;
    for (e = entries; e->lang != NULL; e++) {
        if (is_english(e->lang))
            en = e->text;
        if (e->lang && strcmp(e->lang, "en") == 0 && e->text)
            return e->text;
    }
    return en ? en : "";
}
