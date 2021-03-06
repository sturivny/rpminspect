/*
 * Copyright (C) 2019  Red Hat, Inc.
 * Author(s):  David Cantrell <dcantrell@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "rpminspect.h"

static bool annocheck_driver(struct rpminspect *ri, rpmfile_entry_t *file)
{
    bool result = true;
    const char *arch = NULL;
    string_entry_t *entry = NULL;
    ENTRY e;
    ENTRY *eptr;
    char *after_out = NULL;
    int after_exit;
    char *before_out = NULL;
    int before_exit;
    char *msg = NULL;
    severity_t severity = RESULT_INFO;

    assert(ri != NULL);
    assert(file != NULL);

    /* Ignore files in the SRPM */
    if (headerIsSource(file->rpm_header)) {
        return true;
    }

    /* We need the architecture for reporting */
    arch = get_rpm_header_arch(file->rpm_header);

    /* Only run this check on ELF files */
    if (!is_elf(file->fullpath) || (!is_elf(file->fullpath) && file->peer_file && !is_elf(file->peer_file->fullpath))) {
        return result;
    }

    /* Run each annocheck test and report the results */
    TAILQ_FOREACH(entry, ri->annocheck_keys, items) {
        /* Get the command options for this test */
        e.key = entry->data;
        hsearch_r(e, FIND, &eptr, ri->annocheck_table);

        if (eptr == NULL) {
            continue;
        }

        /* Run the test on the file */
        after_out = run_cmd(&after_exit, ANNOCHECK_CMD, (char *) eptr->data, file->fullpath, NULL);

        /* If we have a before build, run the command on that */
        if (file->peer_file) {
            before_out = run_cmd(&before_exit, ANNOCHECK_CMD, (char *) eptr->data, file->peer_file->fullpath, NULL);
        }

        /* Build a reporting message if we need to */
        if (before_out && after_out) {
            if (before_exit == 0 && after_exit == 0) {
                xasprintf(&msg, "annocheck '%s' test passes for %s on %s", entry->data, file->localpath, arch);
            } else if (before_exit == 1 && after_exit == 0) {
                xasprintf(&msg, "annocheck '%s' test now passes for %s on %s", entry->data, file->localpath, arch);
            } else if (before_exit == 0 && after_exit == 1) {
                xasprintf(&msg, "annocheck '%s' test now fails for %s on %s", entry->data, file->localpath, arch);
                severity = RESULT_VERIFY;
            }
        } else if (after_out) {
            if (after_exit == 0) {
                xasprintf(&msg, "annocheck '%s' test passes for %s on %s", entry->data, file->localpath, arch);
            } else if (after_exit == 1) {
                xasprintf(&msg, "annocheck '%s' test fails for %s on %s", entry->data, file->localpath, arch);
                severity = RESULT_VERIFY;
            }
        }

        /* Report the results */
        if (msg) {
            add_result(ri, severity, WAIVABLE_BY_ANYONE, HEADER_ANNOCHECK, msg, after_out, REMEDY_ANNOCHECK);
            free(msg);
            result = false;
        }

        /* Cleanup */
        free(after_out);
        free(before_out);
        after_out = NULL;
        before_out = NULL;
    }

    return result;
}

/*
 * Main driver for the 'annocheck' inspection.
 */
bool inspect_annocheck(struct rpminspect *ri) {
    bool result;

    assert(ri != NULL);

    /* skip if we have no annocheck tests defined */
    if (ri->annocheck_table == NULL) {
        return true;
    }

    /* run the annocheck tests across all ELF files */
    result = foreach_peer_file(ri, annocheck_driver);

    /* if everything was fine, just say so */
    if (result) {
        add_result(ri, RESULT_OK, NOT_WAIVABLE, HEADER_ANNOCHECK, NULL, NULL, NULL);
    }

    return result;
}
