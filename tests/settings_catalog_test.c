#include <kernel/settings.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(bool condition, const char* name) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        failures++;
    }
}

int main(void) {
    settings_page_id_t matches[SETTINGS_PAGE_ID_LIMIT];

    check(settings_catalog_count() == 20u, "all Settings pages are registered");
    check(settings_catalog_count() ==
          (usize)(SETTINGS_PAGE_ID_LIMIT - 1), "stable page ID range matches catalog");
    check(settings_catalog_validate(), "catalog metadata and IDs validate");
    check(settings_catalog_at(settings_catalog_count()) == NULL,
          "out-of-range catalog index is rejected");
    check(settings_catalog_at(0)->id == SETTINGS_PAGE_DISPLAY,
          "Display remains the first stable catalog entry");
    check(settings_catalog_find(SETTINGS_PAGE_KEYBOARD) != NULL,
          "stable page ID resolves");
    check(settings_catalog_find(SETTINGS_PAGE_INVALID) == NULL,
          "invalid page ID is rejected");
    check(settings_catalog_find(SETTINGS_PAGE_ID_LIMIT) == NULL,
          "sentinel page ID is rejected");

    for (usize i = 0; i < settings_catalog_count(); i++) {
        const settings_page_t* page = settings_catalog_at(i);
        check(page != NULL, "catalog entry exists");
        if (!page) continue;
        check(settings_catalog_find(page->id) == page,
              "ID lookup returns canonical catalog entry");
        for (usize j = i + 1; j < settings_catalog_count(); j++) {
            const settings_page_t* other = settings_catalog_at(j);
            check(other && other->id != page->id, "page IDs are unique");
        }
    }

    usize n = settings_catalog_search("touchpad", matches,
                                     SETTINGS_PAGE_ID_LIMIT);
    check(n == 1u && matches[0] == SETTINGS_PAGE_MOUSE_TOUCHPAD,
          "case-insensitive search finds device page");

    n = settings_catalog_search("PERSONALIZATION", matches,
                                SETTINGS_PAGE_ID_LIMIT);
    check(n == 4u, "group search finds each page in the group");

    n = settings_catalog_search("ssh", matches, SETTINGS_PAGE_ID_LIMIT);
    check(n == 1u && matches[0] == SETTINGS_PAGE_REMOTE_ACCESS,
          "search includes page capability details");

    n = settings_catalog_search("not-a-real-setting", matches,
                                SETTINGS_PAGE_ID_LIMIT);
    check(n == 0u, "unmatched query returns no results");
    check(settings_catalog_search("", matches, SETTINGS_PAGE_ID_LIMIT) == 0u,
          "empty query is rejected");
    check(settings_catalog_search("display", NULL,
                                  SETTINGS_PAGE_ID_LIMIT) == 0u,
          "null result buffer is rejected");
    check(settings_catalog_search("display", matches, 0) == 0u,
          "zero result capacity is rejected");

    n = settings_catalog_search("system", matches, 2);
    check(n == 2u, "search result count is bounded by caller capacity");

    check(strcmp(settings_group_label(SETTINGS_GROUP_SYSTEM), "System") == 0,
          "group label resolves");
    check(strcmp(settings_page_state_label(SETTINGS_PAGE_PARTIAL),
                 "Partially available") == 0,
          "partial state is explicit");
    check(strcmp(settings_page_state_label((settings_page_state_t)99),
                 "Unknown") == 0,
          "invalid state is handled");

    if (failures) {
        fprintf(stderr, "Settings catalog: %d failure(s)\n", failures);
        return 1;
    }
    puts("Settings catalog: all checks passed");
    return 0;
}
