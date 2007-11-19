#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <db.h>

/* verify that the dup flags are written and read from the database file correctly */ 
void test_dup_flags(int dup_flags) {
    printf("test_dup_flags:%d\n", dup_flags);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_dup_flags.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_flags);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);
    r = db->close(db, 0);
    assert(r == 0);

    /* verify dup flags match */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    //assert(r != 0);
    r = db->close(db, 0);
    assert(r == 0);

    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_flags);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);
    r = db->close(db, 0);
    assert(r == 0);

    /* verify nodesize match */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_flags);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);
    r = db->close(db, 0);
    assert(r == 0);
}

DBT *dbt_init(DBT *dbt, void *data, u_int32_t size) {
    memset(dbt, 0, sizeof *dbt);
    dbt->data = data;
    dbt->size = size;
    return dbt;
}

DBT *dbt_init_malloc(DBT *dbt) {
    memset(dbt, 0, sizeof *dbt);
    dbt->flags = DB_DBT_MALLOC;
    return dbt;
}

void expect(DBC *cursor, int k, int v) {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&val), DB_NEXT);
    assert(r == 0);
    assert(key.size == sizeof k);
    int kk;
    memcpy(&kk, key.data, key.size);
    assert(val.size == sizeof v);
    int vv;
    memcpy(&vv, val.data, val.size);
    if (kk != k || vv != v) printf("expect key %d got %d - %d %d\n", htonl(k), htonl(kk), htonl(v), htonl(vv));
    assert(kk == k);
    assert(vv == v);

    free(key.data);
    free(val.data);
}

/* verify that key insertions are stored in insert order */
void test_insert(int n, int dup_mode) {
    printf("test_insert:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_insert.brt";
    int r;
    int i;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    int values[n];
    for (i=0; i<n; i++)
        values[i] = htonl(random());
    int sortvalues[n];
    for (i=0; i<n; i++)
        sortvalues[i] = values[i];
    int mycmp(const void *a, const void *b) {
        return memcmp(a, b, sizeof (int));
    }
    qsort(sortvalues, n, sizeof sortvalues[0], mycmp);
#if USE_BDB
    for (i=1; i<n; i++) 
        if (sortvalues[i-1] == sortvalues[i]) printf("dup %d\n", i);
#endif
    /* insert n-1 unique keys {0, 1,  n-1} - {n/2} */
    for (i=0; i<n; i++) {
        if (i == n/2)
            continue;
        int k = htonl(i);
        int v = values[i];
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    }

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = values[i];
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
#if USE_BDB
        if (r != 0) {
            void find_dup_val(int v, int m) {
                int i;
                printf("dup values[%d]=%d: ", m, v);
                for (i=0; i<m; i++)
                    if (values[i] == v) printf("%d ", i);
                printf("\n");
            }
            find_dup_val(values[i], i);
        }
#endif
        assert(r == 0);
    } 

    /* verify lookups */
    for (i=0; i<n; i++) {
        int k = htonl(i);
        DBT key, val;
        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        if (i == n/2) {
            if (dup_mode & DB_DUPSORT)
                assert(vv == sortvalues[0]);
            else if (dup_mode & DB_DUP)
                assert(vv == values[0]);
            else
                assert(vv == values[n-1]);
        } else
            assert(vv == values[i]);
        free(val.data);
    }

    /* verify the sort order with a cursor */
    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n/2; i++)
        expect(cursor, htonl(i), values[i]);

    if (dup_mode & DB_DUPSORT) {
        for (i=0; i<n; i++) 
            expect(cursor, htonl(n/2), sortvalues[i]);
    } else if (dup_mode & DB_DUP) {
        for (i=0; i<n; i++)
            expect(cursor, htonl(n/2), values[i]);
    } else {
        expect(cursor, htonl(n/2), values[n-1]);
    }

    for (i=(n/2)+1; i<n; i++)
        expect(cursor, htonl(i), values[i]);

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* verify dup keys are buffered in order in non-leaf nodes */
void test_nonleaf_insert(int n, int dup_mode) {
    printf("test_nonleaf_insert:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_nonleaf_insert.brt";
    int r;
    int i;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    int values[n];
    for (i=0; i<n; i++)
        values[i] = htonl(random());
    int sortvalues[n];
    for (i=0; i<n; i++)
        sortvalues[i] = values[i];
    int mycmp(const void *a, const void *b) {
        return memcmp(a, b, sizeof (int));
    }
    qsort(sortvalues, n, sizeof sortvalues[0], mycmp);

    /* insert n-1 unique keys {0, 1,  n-1} - {n/2} */
    for (i=0; i<n; i++) {
        if (i == n/2)
            continue;
        int k = htonl(i);
        int v = values[i];
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    }

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = values[i];
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    } 

   /* verify lookups */
    for (i=0; i<n; i++) {
        int k = htonl(i);
        DBT key, val;
        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        if (i == n/2) {
            if (dup_mode & DB_DUPSORT)
                assert(vv == sortvalues[0]);
            else if (dup_mode & DB_DUP)
                assert(vv == values[0]);
            else
                assert(vv == values[n-1]);
        } else
            assert(vv == values[i]);
        free(val.data);
    }

    /* verify the sort order with a cursor */
    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n/2; i++)
        expect(cursor, htonl(i), values[i]);

    if (dup_mode & DB_DUPSORT) {
        for (i=0; i<n; i++) 
            expect(cursor, htonl(n/2), sortvalues[i]);
    } else if (dup_mode & DB_DUP) {
        for (i=0; i<n; i++)
            expect(cursor, htonl(n/2), values[i]);
    } else {
        expect(cursor, htonl(n/2), values[n-1]);
    }

    for (i=(n/2)+1; i<n; i++)
        expect(cursor, htonl(i), values[i]);

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* verify dup keys delete */
void test_dup_delete(int n, int dup_mode) {
    printf("test_dup_delete:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_dup_delete.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n-1 unique keys {0, 1,  n-1} - {n/2} */
    int i;
    for (i=0; i<n; i++) {
        if (i == n/2)
            continue;
        int k = htonl(i);
        int v = htonl(n+i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    }

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(n));
        free(val.data);
    } 

    DBT key; int k = htonl(n/2);
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);

    /* verify lookup fails */
    {
        int k = htonl(n/2);
        DBT key, val;
        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r != 0);
    }

    /* verify all dups are removed using a cursor */
    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n/2; i++) {
        expect(cursor, htonl(i), htonl(n+i));
    }

    for (i=(n/2)+1; i<n; i++) {
        expect(cursor, htonl(i), htonl(n+i));
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

void test_dup_delete_delete(int n) {
    printf("test_dup_delete_delete:%d\n", n);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_dup_delete_delete.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, DB_DUP);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n-1 unique keys {0, 1,  n-1} - {n/2} */
    int i;
    for (i=0; i<n; i++) {
        if (i == n/2)
            continue;
        int k = htonl(i);
        int v = i;
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    }

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, DB_DUP);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = i;
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    } 

    /* delete the dup key */
    DBT key; int k = htonl(n/2);
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);
  
    /* delete again */
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);


    /* verify all dups are remove using a cursor */
    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n/2; i++) {
        expect(cursor, htonl(i), i);
    }

    for (i=(n/2)+1; i<n; i++) {
        expect(cursor, htonl(i), i);
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

void test_dup_delete_insert(int n, int dup_mode) {
    printf("test_dup_delete_insert:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_dup_delete_insert.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n-1 unique keys {0, 1,  n-1} - {n/2} */
    int i;
    for (i=0; i<n; i++) {
        if (i == n/2)
            continue;
        int k = htonl(i);
        int v = i;
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    }

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(0));
        free(val.data);
    } 

    int k = htonl(n/2);
    DBT key;
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(0));
        free(val.data);
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n/2; i++) {
        expect(cursor, htonl(i), i);
    }

    for (i=0; i<n; i++) {
        expect(cursor, htonl(n/2), htonl(i));
    }

    for (i=(n/2)+1; i<n; i++) {
        expect(cursor, htonl(i), i);
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

void test_all_dup_delete_insert(int n) {
    printf("test_all_dup_delete_insert:%d\n", n);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_all_dup_delete_insert.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, DB_DUP);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = i;
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    }

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, DB_DUP);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = n+i;
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    } 

    DBT key; int k = htonl(n/2);
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = 2*n+i;
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n; i++) {
        expect(cursor, htonl(n/2), 2*n+i);
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

void test_walk_empty(int n, int dup_mode) {
    printf("test_walk_empty:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_walk_empty.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);
    } 

    {
    DBT key; int k = htonl(n/2);
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);
    }

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    {
    DBT key, val;
    int r = cursor->c_get(cursor, dbt_init_malloc(&key), dbt_init_malloc(&val), DB_NEXT);
    assert(r != 0);
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* insert, close, delete, insert, search */
void test_icdi_search(int n, int dup_mode) {
    printf("test_icdi_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_icdi_search.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(0));
        free(val.data);
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    int k = htonl(n/2);
    DBT key;
    r = db->del(db, null_txn, dbt_init(&key, &k, sizeof k), 0);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(n));
        free(val.data);
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<n; i++) {
        expect(cursor, htonl(n/2), htonl(n+i));
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

/* insert, close, insert, search */
void test_ici_search(int n, int dup_mode) {
    printf("test_ici_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_ici_search.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(0));
        free(val.data);
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert n duplicates */
    for (i=0; i<n; i++) {
        int k = htonl(n/2);
        int v = htonl(n+i);
        DBT key, val;
        r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
        assert(r == 0);

        r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
        assert(r == 0);
        int vv;
        assert(val.size == sizeof vv);
        memcpy(&vv, val.data, val.size);
        assert(vv == htonl(0));
        free(val.data);
    } 

    DBC *cursor;
    r = db->cursor(db, null_txn, &cursor, 0);
    assert(r == 0);

    for (i=0; i<2*n; i++) {
        expect(cursor, htonl(n/2), htonl(i));
    }

    r = cursor->c_close(cursor);
    assert(r == 0);

    r = db->close(db, 0);
    assert(r == 0);
}

void db_insert(DB *db, int k, int v) {
    DB_TXN * const null_txn = 0;
    DBT key, val;
    int r = db->put(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), 0);
    assert(r == 0);
}

void expect_db_lookup(DB *db, int k, int v) {
    DB_TXN * const null_txn = 0;
    DBT key, val;
    int r = db->get(db, null_txn, dbt_init(&key, &k, sizeof k), dbt_init_malloc(&val), 0);
    assert(r == 0);
    int vv;
    assert(val.size == sizeof vv);
    memcpy(&vv, val.data, val.size);
    assert(vv == v);
    free(val.data);
}

/* insert 0, insert 1, close, insert 0, search 0 */
void test_i0i1ci0_search(int n, int dup_mode) {
    printf("test_i0i1ci0_search:%d %d\n", n, dup_mode);

    DB_ENV * const null_env = 0;
    DB *db;
    DB_TXN * const null_txn = 0;
    const char * const fname = DIR "/" "test_i0i1ci0.brt";
    int r;

    unlink(fname);

    /* create the dup database file */
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, DB_CREATE, 0666);
    assert(r == 0);
    
    /* insert <0,0> */
    db_insert(db, 0, 0);

    /* insert n duplicates */
    int i;
    for (i=0; i<n; i++) {
        int k = htonl(1);
        int v = htonl(i);
        db_insert(db, k, v);
        expect_db_lookup(db, k, htonl(0));
    } 

    /* reopen the database to force nonleaf buffering */
    r = db->close(db, 0);
    assert(r == 0);
    r = db_create(&db, null_env, 0);
    assert(r == 0);
    r = db->set_flags(db, dup_mode);
    assert(r == 0);
    r = db->set_pagesize(db, 4096);
    assert(r == 0);
    r = db->open(db, null_txn, fname, "main", DB_BTREE, 0, 0666);
    assert(r == 0);

    /* insert <0,1> */
    db_insert(db, 0, 1);

    /* verify dup search digs deep into the tree */
    expect_db_lookup(db, 0, 0);

    r = db->close(db, 0);
    assert(r == 0);
}

int main() {
    int i;

    system("rm -rf " DIR);
    mkdir(DIR, 0777);

    /* test flags */
    test_dup_flags(DB_DUP);
    test_dup_flags(DB_DUP + DB_DUPSORT);
    
    /* nodup tests */
    for (i = 1; i <= (1<<16); i *= 2) {
        test_insert(i, 0);
        test_nonleaf_insert(i, 0);
    }

    /* dup tests */
    for (i = 1; i <= (1<<16); i *= 2) {
        test_insert(i, DB_DUP);
        test_dup_delete(i, DB_DUP);
        test_nonleaf_insert(i, DB_DUP);
        test_dup_delete_insert(i, DB_DUP);
        test_walk_empty(i, DB_DUP);
    }

    /* dup search */
    for (i = 1; i <= (1<<16); i *= 2) {
         test_ici_search(i, DB_DUP);
         test_icdi_search(i, DB_DUP);
         test_i0i1ci0_search(i, DB_DUP);
    }

    /* dupsort tests */
    for (i = 1; i <= (1<<16); i *= 2) {
        test_insert(i, DB_DUP + DB_DUPSORT);
        test_dup_delete(i, DB_DUP + DB_DUPSORT);
        test_nonleaf_insert(i, DB_DUP + DB_DUPSORT);
        test_dup_delete_insert(i, DB_DUP + DB_DUPSORT);
        test_walk_empty(i, DB_DUP + DB_DUPSORT);
    }

    for (i = 1; i <= (1<<16); i *= 2) {
        test_all_dup_delete_insert(i);
    }

    return 0;
}
