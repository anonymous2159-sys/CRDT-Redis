//
// Created by user on 18-9-16.
//

#include "CRDT.h"
#include "RWFramework.h"
#include "lamport_clock.h"
#include "server.h"

#ifdef CRDT_OVERHEAD
/*
#define SUF_OZETOTAL "ozetotal"
#define SUF_ASET "ozaset"
#define SUF_RSET "ozrset"
static redisDb *cur_db = NULL;
static sds cur_tname = NULL;
*/

#define OZE_SIZE (sizeof(oze) + 2 * sizeof(list))
#define OZE_ASE_SIZE (sizeof(oz_ase) + sizeof(lc) + sizeof(listNode))
#define OZE_RSE_SIZE (sizeof(lc) + sizeof(listNode))

#endif

#define ORI_RPQ_TABLE_SUFFIX "_ozets_"
#define LOOKUP(e) (listLength((e)->aset) != 0)
#define SCORE(e) \
    (((e)->innate == NULL ? 0 : (e)->innate->x) + ((e)->acquired == NULL ? 0 : (e)->acquired->inc))

typedef struct ozset_aset_element
{
    lc *t;
    double x;
    double inc;
    double count;
} oz_ase;

typedef struct ORI_RPQ_element
{
    int current;
    oz_ase *innate;
    oz_ase *acquired;
    list *aset;
    list *rset;
} oze;

sds oz_aseToSds(oz_ase *a)
{
    return sdscatprintf(sdsempty(), "(%d,%d),%f,%f,%f", a->t->x, a->t->id, a->x, a->inc, a->count);
}

oze *ozeNew()
{
#ifdef CRDT_OVERHEAD
    // inc_ovhd_count(cur_db, cur_tname, SUF_OZETOTAL, 1);
    ovhd_inc(OZE_SIZE);
#endif
    oze *e = zmalloc(sizeof(oze));
    e->current = 0;
    e->innate = NULL;
    e->acquired = NULL;
    e->aset = listCreate();
    e->rset = listCreate();
    return e;
}

/*
#ifdef CRDT_OVERHEAD

robj *_get_ovhd_count(redisDb *db, sds tname, const char *suf)
{
    robj *logname = createObject(OBJ_STRING, sdscat(sdsdup(tname), suf));
    robj *o;
    if ((o = lookupKeyWrite(db, logname)) == NULL)
    {
        o = createObject(OBJ_STRING, 0);
        o->encoding = OBJ_ENCODING_INT;
        dbAdd(db, logname, o);
    }
    decrRefCount(logname);
    return o;
}

void inc_ovhd_count(redisDb *db, sds tname, const char *suf, long i)
{
    robj *o = _get_ovhd_count(db, tname, suf);
    o->ptr = (void *) ((long) o->ptr + i);
}

long get_ovhd_count(redisDb *db, sds tname, const char *suf)
{
    robj *o = _get_ovhd_count(db, tname, suf);
    return (long) (o->ptr);
}

#endif
*/

oz_ase *asetGet(oze *e, lc *t, int delete)
{
    listNode *ln;
    listIter li;
    listRewind(e->aset, &li);
    while ((ln = listNext(&li)))
    {
        oz_ase *a = ln->value;
        if (lc_cmp_as_tag(t, a->t) == 0)
        {
            if (delete)
            {
                listDelNode(e->aset, ln);
#ifdef CRDT_OVERHEAD
                // inc_ovhd_count(cur_db, cur_tname, SUF_ASET, -1);
                ovhd_inc(-OZE_ASE_SIZE);
#endif
            }
            return a;
        }
    }
    return NULL;
}

lc *rsetGet(oze *e, lc *t, int delete)
{
    listNode *ln;
    listIter li;
    listRewind(e->rset, &li);
    while ((ln = listNext(&li)))
    {
        lc *a = ln->value;
        if (lc_cmp_as_tag(t, a) == 0)
        {
            if (delete)
            {
                listDelNode(e->rset, ln);
#ifdef CRDT_OVERHEAD
                // inc_ovhd_count(cur_db, cur_tname, SUF_RSET, -1);
                ovhd_inc(-OZE_RSE_SIZE);
#endif
            }
            return a;
        }
    }
    return NULL;
}

// 下面函数对自己参数 t 没有所有权
oz_ase *add_ase(oze *e, lc *t)
{
    oz_ase *a = zmalloc(sizeof(oz_ase));
    a->t = lc_dup(t);
    a->inc = 0;
    a->count = 0;
    listAddNodeTail(e->aset, a);
#ifdef CRDT_OVERHEAD
    // inc_ovhd_count(cur_db, cur_tname, SUF_ASET, 1);
    ovhd_inc(OZE_ASE_SIZE);
#endif
    return a;
}

int update_innate_value(oze *e, lc *t, double v)
{
    if (rsetGet(e, t, 0) != NULL) return 0;
    oz_ase *a = asetGet(e, t, 0);
    if (a == NULL) a = add_ase(e, t);
    a->x = v;
    if (e->innate == NULL || lc_cmp_as_tag(e->innate->t, a->t) < 0)
    {
        e->innate = a;
        return 1;
    }
    return 0;
}

int update_acquired_value(oze *e, lc *t, double v)
{
    if (rsetGet(e, t, 0) != NULL) return 0;
    oz_ase *a = asetGet(e, t, 0);
    if (a == NULL) a = add_ase(e, t);
    a->inc += v;
    a->count += (v > 0) ? v : -v;
    if (e->acquired == a) return 1;
    if (e->acquired == NULL || e->acquired->count < a->count
        || (e->acquired->count == a->count && lc_cmp_as_tag(e->acquired->t, a->t) < 0))
    {
        e->acquired = a;
        return 1;
    }
    return 0;
}

// 没有整理
void remove_tag(oze *e, lc *t)
{
    if (rsetGet(e, t, 0) != NULL) return;
    lc *nt = lc_dup(t);
    listAddNodeTail(e->rset, nt);
#ifdef CRDT_OVERHEAD
    // inc_ovhd_count(cur_db, cur_tname, SUF_RSET, 1);
    ovhd_inc(OZE_RSE_SIZE);

#endif
    oz_ase *a;
    if ((a = asetGet(e, t, 1)) != NULL)
    {
        if (e->innate == a) e->innate = NULL;
        if (e->acquired == a) e->acquired = NULL;
        zfree(a->t);
        zfree(a);
    }
}

void resort(oze *e)
{
    if (e->innate != NULL && e->acquired != NULL) return;

    listNode *ln;
    listIter li;
    listRewind(e->aset, &li);

    while ((ln = listNext(&li)))
    {
        oz_ase *a = ln->value;
        if (e->innate == NULL || lc_cmp_as_tag(e->innate->t, a->t) < 0) e->innate = a;
        if (e->acquired == NULL || e->acquired->count < a->count
            || (e->acquired->count == a->count && lc_cmp_as_tag(e->acquired->t, a->t) < 0))
            e->acquired = a;
    }
}

#define GET_OZE(arg_t, create)                                                     \
    (oze *)rehHTGet(c->db, c->arg_t[1], ORI_RPQ_TABLE_SUFFIX, c->arg_t[2], create, \
                    (rehNew_func_t)ozeNew)

robj *getZsetOrCreate(redisDb *db, robj *zset_name, robj *element_name)
{
    robj *zobj = lookupKeyWrite(db, zset_name);
    if (zobj == NULL)
    {
        if (server.zset_max_ziplist_entries == 0
            || server.zset_max_ziplist_value < sdslen(element_name->ptr))
        { zobj = createZsetObject(); }
        else
        {
            zobj = createZsetZiplistObject();
        }
        dbAdd(db, zset_name, zobj);
    }
    return zobj;
}

void ozaddCommand(client *c)
{
/*
#ifdef CRDT_OVERHEAD
    PRE_SET;
#endif
*/
    CRDT_BEGIN
        CRDT_PREPARE
            CHECK_ARGC(4);
            CHECK_CONTAINER_TYPE(OBJ_ZSET);
            CHECK_ARG_TYPE_DOUBLE(c->argv[3]);
            oze *e = GET_OZE(argv, 1);
            if (LOOKUP(e))
            {
                addReply(c, shared.ele_exist);
                return;
            }
            lc *t = lc_new(e->current);
            e->current++;
            RARGV_ADD_SDS(lcToSds(t));
            lc_delete(t);
        CRDT_EFFECT
            double v;
            getDoubleFromObject(c->rargv[3], &v);
            lc *t = sdsToLc(c->rargv[4]->ptr);
            oze *e = GET_OZE(rargv, 1);
            if (update_innate_value(e, t, v))
            {
                robj *zset = getZsetOrCreate(c->db, c->rargv[1], c->rargv[2]);
                int flags = ZADD_NONE;
                zsetAdd(zset, SCORE(e), c->rargv[2]->ptr, &flags, NULL);
            }
            lc_delete(t);
            server.dirty++;
    CRDT_END
}

void ozincrbyCommand(client *c)
{
/*
#ifdef CRDT_OVERHEAD
    PRE_SET;
#endif
*/
    CRDT_BEGIN
        CRDT_PREPARE
            CHECK_ARGC(4);
            CHECK_CONTAINER_TYPE(OBJ_ZSET);
            CHECK_ARG_TYPE_DOUBLE(c->argv[3]);
            oze *e = GET_OZE(argv, 0);
            if (e == NULL || !LOOKUP(e))
            {
                addReply(c, shared.ele_nexist);
                return;
            }
            listNode *ln;
            listIter li;
            listRewind(e->aset, &li);
            while ((ln = listNext(&li)))
            {
                oz_ase *a = ln->value;
                RARGV_ADD_SDS(lcToSds(a->t));
            }
        CRDT_EFFECT
            double v;
            getDoubleFromObject(c->rargv[3], &v);
            oze *e = GET_OZE(rargv, 1);
            int changed = 0;
            for (int i = 4; i < c->rargc; i++)
            {
                lc *t = sdsToLc(c->rargv[i]->ptr);
                changed += update_acquired_value(e, t, v);
                lc_delete(t);
            }
            if (changed)
            {
                robj *zset = getZsetOrCreate(c->db, c->rargv[1], c->rargv[2]);
                int flags = ZADD_NONE;
                zsetAdd(zset, SCORE(e), c->rargv[2]->ptr, &flags, NULL);
            }
            server.dirty++;
    CRDT_END
}

void ozremCommand(client *c)
{
/*
#ifdef CRDT_OVERHEAD
    PRE_SET;
#endif
*/
    CRDT_BEGIN
        CRDT_PREPARE
            CHECK_ARGC(3);
            CHECK_CONTAINER_TYPE(OBJ_ZSET);
            oze *e = GET_OZE(argv, 0);
            if (e == NULL || !LOOKUP(e))
            {
                addReply(c, shared.ele_nexist);
                return;
            }
            listNode *ln;
            listIter li;
            listRewind(e->aset, &li);
            while ((ln = listNext(&li)))
            {
                oz_ase *a = ln->value;
                RARGV_ADD_SDS(lcToSds(a->t));
            }
        CRDT_EFFECT
            oze *e = GET_OZE(rargv, 1);
            for (int i = 3; i < c->rargc; i++)
            {
                lc *t = sdsToLc(c->rargv[i]->ptr);
                remove_tag(e, t);
                lc_delete(t);
            }
            if (e->innate == NULL || e->acquired == NULL)
            {
                resort(e);
                robj *zset = getZsetOrCreate(c->db, c->rargv[1], c->rargv[2]);
                if (LOOKUP(e))
                {
                    int flags = ZADD_NONE;
                    zsetAdd(zset, SCORE(e), c->rargv[2]->ptr, &flags, NULL);
                }
                else
                {
                    zsetDel(zset, c->rargv[2]->ptr);
                }
            }
            server.dirty++;
    CRDT_END
}

void ozscoreCommand(client *c)
{
    robj *key = c->argv[1];
    robj *zobj;
    double score;

    if ((zobj = lookupKeyReadOrReply(c, key, shared.null[c->resp])) == NULL
        || checkType(c, zobj, OBJ_ZSET))
        return;

    if (zsetScore(zobj, c->argv[2]->ptr, &score) == C_ERR) { addReply(c, shared.null[c->resp]); }
    else
    {
        addReplyDouble(c, score);
    }
}

void ozmaxCommand(client *c)
{
    robj *zobj;
    if ((zobj = lookupKeyReadOrReply(c, c->argv[1], shared.emptyarray)) == NULL
        || checkType(c, zobj, OBJ_ZSET))
        return;
    if (zsetLength(zobj) == 0)
    {
        addReply(c, shared.emptyarray);

#ifdef CRDT_LOG
        CRDT_log("%s %s, NONE", (char *)(c->argv[0]->ptr), (char *)(c->argv[1]->ptr));
#endif

        return;
    }
    addReplyArrayLen(c, 2);
    if (zobj->encoding == OBJ_ENCODING_ZIPLIST)
    {
        unsigned char *zl = zobj->ptr;
        unsigned char *eptr, *sptr;
        unsigned char *vstr;
        unsigned int vlen;
        long long vlong;

        eptr = ziplistIndex(zl, -2);
        sptr = ziplistNext(zl, eptr);

        serverAssertWithInfo(c, zobj, eptr != NULL && sptr != NULL);
        serverAssertWithInfo(c, zobj, ziplistGet(eptr, &vstr, &vlen, &vlong));
        if (vstr == NULL)
            addReplyBulkLongLong(c, vlong);
        else
            addReplyBulkCBuffer(c, vstr, vlen);
        addReplyDouble(c, zzlGetScore(sptr));

#ifdef CRDT_LOG
        if (vstr == NULL)
            CRDT_log("%s %s, %ld: %f", (char *)(c->argv[0]->ptr), (char *)(c->argv[1]->ptr),
                     (long)vlong, zzlGetScore(sptr));
        else
        {
            char *temp = zmalloc(sizeof(char) * (vlen + 1));
            for (unsigned int i = 0; i < vlen; ++i)
                temp[i] = vstr[i];
            temp[vlen] = '\0';
            CRDT_log("%s %s, %s: %f", (char *)(c->argv[0]->ptr), (char *)(c->argv[1]->ptr), temp,
                     zzlGetScore(sptr));
            zfree(temp);
        }
#endif
    }
    else if (zobj->encoding == OBJ_ENCODING_SKIPLIST)
    {
        zset *zs = zobj->ptr;
        zskiplist *zsl = zs->zsl;
        zskiplistNode *ln = zsl->tail;
        serverAssertWithInfo(c, zobj, ln != NULL);
        sds ele = ln->ele;
        addReplyBulkCBuffer(c, ele, sdslen(ele));
        addReplyDouble(c, ln->score);

#ifdef CRDT_LOG
        CRDT_log("%s %s, %s: %f", (char *)(c->argv[0]->ptr), (char *)(c->argv[1]->ptr), ele,
                 ln->score);
#endif
    }
    else
    {
        serverPanic("Unknown sorted set encoding");
    }
}

#ifdef CRDT_ELE_STATUS
void ozestatusCommand(client *c)
{
    oze *e = GET_OZE(argv, 0);
    if (e == NULL)
    {
        addReply(c, shared.emptyarray);
        return;
    }

    unsigned long len = 6 + listLength(e->aset) + listLength(e->rset);
    addReplyArrayLen(c, len);

    addReplyBulkSds(c, sdscatprintf(sdsempty(), "current:%d", e->current));

    addReplyBulkSds(c, sdsnew("innate,acquired:"));
    if (e->innate == NULL)
        addReply(c, shared.emptybulk);
    else
        addReplyBulkSds(c, oz_aseToSds(e->innate));
    if (e->acquired == NULL)
        addReply(c, shared.emptybulk);
    else
        addReplyBulkSds(c, oz_aseToSds(e->acquired));

    listNode *ln;
    listIter li;

    addReplyBulkSds(c, sdsnew("Add Set:"));
    listRewind(e->aset, &li);
    while ((ln = listNext(&li)))
    {
        oz_ase *a = ln->value;
        addReplyBulkSds(c, oz_aseToSds(a));
    }

    addReplyBulkSds(c, sdsnew("Remove Set:"));
    listRewind(e->rset, &li);
    while ((ln = listNext(&li)))
    {
        lc *a = ln->value;
        addReplyBulkSds(c, lcToSds(a));
    }
}
#endif

#ifdef CRDT_OPCOUNT
void ozopcountCommand(client *c) { addReplyLongLong(c, op_count_get()); }
#endif

/* Actually the hash set used here to store oze structures is not necessary.
 * We can store oze in the zset, for it's whether ziplist or dict+skiplist.
 * We use the hash set here for fast implementing our CRDT Algorithms.
 * We may optimize our implementation by not using the hash set and using
 * zset's own dict instead in the future.
 * As for metadata overhead calculation, we here do it as if we have done
 * such optimization. The commented area is the overhead if we take the
 * hash set into account.
 *
 * optimized:
 * zset:
 * key -> score(double)
 * --->
 * key -> pointer that point to metadata (oze*)
 *
 * the metadata contains score information
 * overall the metadata overhead is size used by oze
 * */
#ifdef CRDT_OVERHEAD

void ozoverheadCommand(client *c)
{
    /*
    PRE_SET;
    long long size = get_ovhd_count(cur_db, cur_tname, SUF_OZETOTAL) * OZE_SIZE
                     + get_ovhd_count(cur_db, cur_tname, SUF_ASET) * OZE_ASE_SIZE
                     + get_ovhd_count(cur_db, cur_tname, SUF_RSET) * OZE_RSE_SIZE;
     */
    addReplyLongLong(c, ovhd_get());
}

#elif 0

#define OZESIZE(e) \
    (OZE_SIZE + listLength((e)->aset) * OZE_ASE_SIZE + listLength((e)->rset) * OZE_RSE_SIZE)

void ozoverheadCommand(client *c)
{
    robj *htname = createObject(OBJ_STRING, sdscat(sdsdup(c->argv[1]->ptr), ORI_RPQ_TABLE_SUFFIX));
    robj *ht = lookupKeyRead(c->db, htname);
    long long size = 0;

    /*
     * The overhead for database to store the hash set information.
     * sds temp = sdsdup(htname->ptr);
     * size += sizeof(dictEntry) + sizeof(robj) + sdsAllocSize(temp);
     * sdsfree(temp);
     */

    decrRefCount(htname);
    if (ht == NULL)
    {
        addReplyLongLong(c, 0);
        return;
    }

    hashTypeIterator *hi = hashTypeInitIterator(ht);
    while (hashTypeNext(hi) != C_ERR)
    {
        sds value = hashTypeCurrentObjectNewSds(hi, OBJ_HASH_VALUE);
        oze *e = *(oze **)value;
        size += OZESIZE(e);
        sdsfree(value);
    }
    hashTypeReleaseIterator(hi);
    addReplyLongLong(c, size);
    /*
    if (ht->encoding == OBJ_ENCODING_ZIPLIST)
    {
        // Not implemented. We show the overhead calculation method:
        // size += (size of the ziplist structure itself) + (size of keys and values);
        // Iterate the ziplist to get each oze* e;
        // size += OZESIZE(e);
    }
    else if (ht->encoding == OBJ_ENCODING_HT)
    {
        dict *d = ht->ptr;
        size += sizeof(dict) + sizeof(dictType) + (d->ht[0].size + d->ht[1].size) * sizeof(dictEntry
    *)
                + (d->ht[0].used + d->ht[1].used) * sizeof(dictEntry);

        dictIterator *di = dictGetIterator(d);
        dictEntry *de;
        while ((de = dictNext(di)) != NULL)
        {
            sds key = dictGetKey(de);
            sds value = dictGetVal(de);
            size += sdsAllocSize(key) + sdsAllocSize(value);
            oze *e = *(oze **) value;
            size += OZESIZE(e);
        }
        dictReleaseIterator(di);
    }
    else
    {
        serverPanic("Unknown hash encoding");
    }
    */
}

#endif
