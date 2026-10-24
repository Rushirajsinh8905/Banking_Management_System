#include "auth.h"
#include "common.h"
#include "records.h"
#include "session_manager.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

/* NOTE: the old version printed every username AND plaintext password it read
 * to the server console ("DEBUG: Read line ..."). Those debug prints were
 * removed - logging passwords is a serious security problem. */
int authenticate(int role, const char *username, const char *password, int *user_id) {
    if (!role_is_valid(role)) return AUTH_FAIL;

    AccountRecord rec;
    int found;

    db_rdlock(role_db(role));
    found = find_account_by_username(role, username, &rec);
    db_unlock(role_db(role));

    if (!found || strcmp(rec.password, password) != 0) return AUTH_FAIL;
    if (role == ROLE_CUSTOMER && rec.active == ACCOUNT_CLOSED) return AUTH_CLOSED;
    if (role == ROLE_CUSTOMER && rec.active != ACCOUNT_ACTIVE) return AUTH_INACTIVE;

    if (!check_and_register_login(role, rec.id)) return AUTH_ALREADY_LOGGED_IN;

    *user_id = rec.id;
    return AUTH_OK;
}
