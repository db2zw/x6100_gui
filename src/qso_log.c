/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2024 Georgy Dyuldin aka R2RFE
 */

#include "qso_log.h"

#include "util.h"
#include "msg.h"
#include "adif.h"

#include <lvgl/src/misc/lv_log.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>

static sqlite3_stmt     *search_callsign_stmt=NULL;
static sqlite3          *db = NULL;


static bool create_tables();
static void* import_adif_thread(void* args);


bool qso_log_init() {
    int rc = sqlite3_open("/mnt/qso_log.db", &db);

    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Can't open qso_log.db");
        return false;
    }
    return create_tables();
}

void qso_log_destruct() {
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
}

void qso_log_import_adif(const char * path) {
    pthread_t thr;

    if (access(path, F_OK) != 0) {
        LV_LOG_INFO("No ADI file to import");
        return;
    }
    if(pthread_create(&thr, NULL, import_adif_thread, (void*)path) != 0) {
        LV_LOG_ERROR("Import adif thread start failed");
    }
}


qso_log_record_t qso_log_record_create(const char * local_call, const char * remote_call,
    time_t qso_time, const char * mode, int rsts, int rstr, float freq_mhz, const char * band,
    const char * name, const char * qth, const char *local_grid, const char * remote_grid)
{
    qso_log_record_t rec = {
        .time = qso_time,
        .rsts = rsts,
        .rstr = rstr,
        .freq_mhz = freq_mhz,
    };
    strncpy(rec.local_call, local_call, sizeof(rec.local_call) - 1);
    rec.local_call[sizeof(rec.local_call) - 1] = 0;

    strncpy(rec.remote_call, remote_call, sizeof(rec.remote_call) - 1);
    rec.remote_call[sizeof(rec.remote_call) - 1] = 0;

    strncpy(rec.band, band, sizeof(rec.band) - 1);
    rec.band[sizeof(rec.band) - 1] = 0;

    strncpy(rec.mode, mode, sizeof(rec.mode) - 1);
    rec.mode[sizeof(rec.mode) - 1] = 0;

    if (name) {
        strncpy(rec.name, name, sizeof(rec.name) - 1);
        rec.name[sizeof(rec.name) - 1] = 0;
    }

    if (qth) {
        strncpy(rec.qth, qth, sizeof(rec.qth) - 1);
        rec.qth[sizeof(rec.qth) - 1] = 0;
    }

    if (local_grid) {
        strncpy(rec.local_grid, local_grid, sizeof(rec.local_grid) - 1);
        rec.local_grid[sizeof(rec.local_grid) - 1] = 0;
    }

    if (remote_grid) {
        strncpy(rec.remote_grid, remote_grid, sizeof(rec.remote_grid) - 1);
        rec.remote_grid[sizeof(rec.remote_grid) - 1] = 0;
    }
    return rec;
}


static inline int bind_optional_text(sqlite3_stmt * stmt, int pos, const char * val) {
    if (!val) {
        return sqlite3_bind_null(stmt, pos);
    } else {
        return sqlite3_bind_text(stmt, pos, val, strlen(val), 0);
    }
}

int qso_log_record_save(qso_log_record_t qso) {
    sqlite3_stmt    *stmt;
    int             rc;

    if (strlen(qso.local_call) == 0) {
        LV_LOG_ERROR("Local callsign is required");
        return -1;
    }
    if (strlen(qso.remote_call) == 0) {
        LV_LOG_ERROR("Remote callsign is required");
        return -1;
    }
    if (strlen(qso.mode) == 0) {
        LV_LOG_ERROR("Modulation is required");
        return -1;
    }
    if (strlen(qso.band) == 0) {
        LV_LOG_ERROR("Band is required");
        return -1;
    }

    rc = sqlite3_prepare_v2(
        db, "INSERT OR IGNORE INTO qso_log ("
                "ts, freq, band, mode, local_callsign, remote_callsign, rsts, rstr, "
                "local_grid, remote_grid, op_name, canonized_remote_callsign"
            ") VALUES (datetime(:ts, 'unixepoch'), :freq, :band, :mode, :local_callsign, :remote_callsign, "
                ":rsts, :rstr, :local_grid, :remote_grid, :op_name, :canonized_remote_callsign)",
                       -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR("Error in prepairing query");
        return -1;
    }
    rc = sqlite3_bind_int64(stmt, sqlite3_bind_parameter_index(stmt, ":ts"), qso.time);
    if (rc != SQLITE_OK) {
        printf("check point: %i\n", rc);
        printf("column_id: %i\n", sqlite3_bind_parameter_index(stmt, ":ts"));
        fflush(stdout);
        return -1;
    }
    rc = sqlite3_bind_double(stmt, sqlite3_bind_parameter_index(stmt, ":freq"), (double) qso.freq_mhz);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":band"), qso.band, strlen(qso.band), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":mode"), qso.mode, strlen(qso.mode), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":local_callsign"), qso.local_call, strlen(qso.local_call), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, ":remote_callsign"), qso.remote_call, strlen(qso.remote_call), 0);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":rsts"), qso.rsts);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, ":rstr"), qso.rstr);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":local_grid"), qso.local_grid);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":remote_grid"), qso.remote_grid);
    if (rc != SQLITE_OK) return -1;
    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":op_name"), qso.name);
    if (rc != SQLITE_OK) return -1;

    char * canonized_remote_callsign = util_canonize_callsign(qso.remote_call, true);
    if (!canonized_remote_callsign) {
        canonized_remote_callsign = strdup(qso.remote_call);
    }

    rc = bind_optional_text(stmt, sqlite3_bind_parameter_index(stmt, ":canonized_remote_callsign"), canonized_remote_callsign);
    if (rc != SQLITE_OK) {
        free(canonized_remote_callsign);
        return -1;
    }

    if(sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Statement: %s\n", sqlite3_expanded_sql(stmt));
        free(canonized_remote_callsign);
        return -1;
    }
    free(canonized_remote_callsign);

    sqlite3_finalize(stmt);
    return 0;
}


qso_log_search_worked_t qso_log_search_worked(const char *callsign, const char * mode, const char * band)
{
    int                         rc;
    qso_log_search_worked_t     worked = SEARCH_WORKED_NO;

    if (!search_callsign_stmt) {
        rc = sqlite3_prepare_v3(db, "SELECT DISTINCT band, mode FROM qso_log WHERE canonized_remote_callsign LIKE ?",
                        -1, SQLITE_PREPARE_PERSISTENT, &search_callsign_stmt, 0);
        if (rc != SQLITE_OK) {
            return -1;
        }
    } else {
        sqlite3_reset(search_callsign_stmt);
        sqlite3_clear_bindings(search_callsign_stmt);
    }

    char * canonized_callsign = util_canonize_callsign(callsign, true);
    if (!canonized_callsign) {
        canonized_callsign = strdup(callsign);
    }
    rc = sqlite3_bind_text(search_callsign_stmt, 1, canonized_callsign, strlen(canonized_callsign), 0);
    if (rc != SQLITE_OK) {
        free(canonized_callsign);
        return -1;
    }

    while (sqlite3_step(search_callsign_stmt) != SQLITE_DONE) {
        worked = SEARCH_WORKED_YES;
        if ((strcmp(band, sqlite3_column_text(search_callsign_stmt, 0)) == 0) &&
            (strcmp(mode, sqlite3_column_text(search_callsign_stmt, 1)) == 0))
        {
            worked = SEARCH_WORKED_SAME_MODE;
            break;
        }
    }

    free(canonized_callsign);
    return worked;
}


static void * import_adif_thread(void* args) {

    char *path = (char* )args;

    pthread_detach(pthread_self());

    qso_log_record_t * records;
    int cnt = adif_read(path, &records);
    char * canonized;
    size_t updated_rows = 0;
    size_t c = 0;
    for (size_t i = 0; i < cnt; i++) {
        if (qso_log_record_save(records[i]) == 0) {
            updated_rows++;
            c++;
        }
        if (c >= 10) {
            c = 0;
            msg_set_text_fmt("Importing QSO: %zu/%zu", updated_rows, cnt);
            msg_set_timeout(5000);
        }
    }
    free(records);

    char new_path[128] = {0};
    snprintf(new_path, sizeof(new_path), "%s.bak", path);
    rename(path, new_path);
    msg_set_text_fmt("Imported %zu QSOs from %zu", updated_rows, cnt);
    msg_set_timeout(2000);
    pthread_exit(NULL);
}



static bool create_tables() {
    char    *err = 0;
    int     rc;

    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS qso_log( "
            "ts              TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
            "freq            REAL CHECK ( freq > 0 ), "
            "band            TEXT NOT NULL, "
            "mode            TEXT CHECK ( mode IN ('SSB', 'CW', 'FT8', 'FT4', 'AM', 'FM', 'MFSK')), "
            "local_callsign  TEXT NOT NULL, "
            "remote_callsign TEXT NOT NULL, "
            "canonized_remote_callsign TEXT NOT NULL, "
            "rsts            INTEGER NOT NULL, "
            "rstr            INTEGER NOT NULL, "
            "local_qth       TEXT, "
            "remote_qth      TEXT, "
            "local_grid      TEXT, "
            "remote_grid     TEXT, "
            "op_name         TEXT, "
            "comment         TEXT "
        ")",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }

    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS qso_log_idx_canonized_remote_callsign ON qso_log(canonized_remote_callsign COLLATE NOCASE)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }
    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS qso_log_idx_mode ON qso_log(mode)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }
    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS qso_log_idx_ts ON qso_log(ts)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }

    rc = sqlite3_exec(db,
        "CREATE UNIQUE INDEX IF NOT EXISTS qso_log_idx_ts_call ON qso_log(ts, remote_callsign)",
        NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        LV_LOG_ERROR(err);
        return false;
    }

    return true;
}
