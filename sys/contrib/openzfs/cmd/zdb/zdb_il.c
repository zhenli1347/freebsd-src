// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2012 Cyril Plisko. All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

/*
 * Print intent log header and statistics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/spa_impl.h>
#include <sys/abd.h>

#include "zdb.h"

extern uint8_t dump_opt[256];

static char tab_prefix[4] = "\t\t\t";

static void
print_log_bp(const blkptr_t *bp, const char *prefix)
{
	char blkbuf[BP_SPRINTF_LEN];

	snprintf_blkptr(blkbuf, sizeof (blkbuf), bp);
	(void) printf("%s%s\n", prefix, blkbuf);
}

static void
zil_prt_rec_create(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog;
	const lr_create_t *lrc = arg;
	const _lr_create_t *lr = &lrc->lr_create;
	time_t crtime = lr->lr_crtime[0];
	const char *name, *link;
	lr_attr_t *lrattr;

	name = (const char *)&lrc->lr_data[0];

	if (lr->lr_common.lrc_txtype == TX_CREATE_ATTR ||
	    lr->lr_common.lrc_txtype == TX_MKDIR_ATTR) {
		lrattr = (lr_attr_t *)&lrc->lr_data[0];
		name += ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
	}

	if (txtype == TX_SYMLINK) {
		link = (const char *)&lrc->lr_data[strlen(name) + 1];
		(void) printf("%s%s -> %s\n", tab_prefix, name, link);
	} else if (txtype != TX_MKXATTR) {
		(void) printf("%s%s\n", tab_prefix, name);
	}

	(void) printf("%s%s", tab_prefix, ctime(&crtime));
	(void) printf("%sdoid %llu, foid %llu, slots %llu, mode %llo\n",
	    tab_prefix, (u_longlong_t)lr->lr_doid,
	    (u_longlong_t)LR_FOID_GET_OBJ(lr->lr_foid),
	    (u_longlong_t)LR_FOID_GET_SLOTS(lr->lr_foid),
	    (longlong_t)lr->lr_mode);
	(void) printf("%suid %llu, gid %llu, gen %llu, rdev 0x%llx\n",
	    tab_prefix,
	    (u_longlong_t)lr->lr_uid, (u_longlong_t)lr->lr_gid,
	    (u_longlong_t)lr->lr_gen, (u_longlong_t)lr->lr_rdev);
}

static void
zil_prt_rec_remove(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_remove_t *lr = arg;

	(void) printf("%sdoid %llu, name %s\n", tab_prefix,
	    (u_longlong_t)lr->lr_doid, (const char *)&lr->lr_data[0]);
}

static void
zil_prt_rec_link(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_link_t *lr = arg;

	(void) printf("%sdoid %llu, link_obj %llu, name %s\n", tab_prefix,
	    (u_longlong_t)lr->lr_doid, (u_longlong_t)lr->lr_link_obj,
	    (const char *)&lr->lr_data[0]);
}

static void
zil_prt_rec_rename(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_rename_t *lrr = arg;
	const _lr_rename_t *lr = &lrr->lr_rename;
	const char *snm = (const char *)&lrr->lr_data[0];
	const char *tnm = (const char *)&lrr->lr_data[strlen(snm) + 1];

	(void) printf("%ssdoid %llu, tdoid %llu\n", tab_prefix,
	    (u_longlong_t)lr->lr_sdoid, (u_longlong_t)lr->lr_tdoid);
	(void) printf("%ssrc %s tgt %s\n", tab_prefix, snm, tnm);
	switch (txtype) {
	case TX_RENAME_EXCHANGE:
		(void) printf("%sflags RENAME_EXCHANGE\n", tab_prefix);
		break;
	case TX_RENAME_WHITEOUT:
		(void) printf("%sflags RENAME_WHITEOUT\n", tab_prefix);
		break;
	}
}

static int
zil_prt_rec_write_cb(void *data, size_t len, void *unused)
{
	(void) unused;
	char *cdata = data;

	for (size_t i = 0; i < len; i++) {
		if (isprint(*cdata))
			(void) printf("%c ", *cdata);
		else
			(void) printf("%2X", *cdata);
		cdata++;
	}
	return (0);
}

static void
zil_prt_rec_write(zilog_t *zilog, int txtype, const void *arg)
{
	const lr_write_t *lr = arg;
	abd_t *data;
	const blkptr_t *bp = &lr->lr_blkptr;
	zbookmark_phys_t zb;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);
	int error;

	(void) printf("%sfoid %llu, offset %llx, length %llx\n", tab_prefix,
	    (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_offset,
	    (u_longlong_t)lr->lr_length);

	if (txtype == TX_WRITE2 || verbose < 4)
		return;

	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		(void) printf("%shas blkptr, %s\n", tab_prefix,
		    !BP_IS_HOLE(bp) && BP_GET_LOGICAL_BIRTH(bp) >=
		    spa_min_claim_txg(zilog->zl_spa) ?
		    "will claim" : "won't claim");
		print_log_bp(bp, tab_prefix);

		if (verbose < 5)
			return;
		if (BP_IS_HOLE(bp)) {
			(void) printf("\t\t\tLSIZE 0x%llx\n",
			    (u_longlong_t)BP_GET_LSIZE(bp));
			(void) printf("%s<hole>\n", tab_prefix);
			return;
		}
		if (BP_GET_LOGICAL_BIRTH(bp) < zilog->zl_header->zh_claim_txg) {
			(void) printf("%s<block already committed>\n",
			    tab_prefix);
			return;
		}

		ASSERT3U(BP_GET_LSIZE(bp), !=, 0);
		SET_BOOKMARK(&zb, dmu_objset_id(zilog->zl_os),
		    lr->lr_foid, ZB_ZIL_LEVEL,
		    lr->lr_offset / BP_GET_LSIZE(bp));

		data = abd_alloc(BP_GET_LSIZE(bp), B_FALSE);
		error = zio_wait(zio_read(NULL, zilog->zl_spa,
		    bp, data, BP_GET_LSIZE(bp), NULL, NULL,
		    ZIO_PRIORITY_SYNC_READ, ZIO_FLAG_CANFAIL, &zb));
		if (error)
			goto out;
	} else {
		if (verbose < 5)
			return;

		/* data is stored after the end of the lr_write record */
		data = abd_alloc(lr->lr_length, B_FALSE);
		abd_copy_from_buf(data, &lr->lr_data[0], lr->lr_length);
	}

	(void) printf("%s", tab_prefix);
	(void) abd_iterate_func(data,
	    0, MIN(lr->lr_length, (verbose < 6 ? 20 : SPA_MAXBLOCKSIZE)),
	    zil_prt_rec_write_cb, NULL);
	(void) printf("\n");

out:
	abd_free(data);
}

static void
zil_prt_rec_write_enc(zilog_t *zilog, int txtype, const void *arg)
{
	(void) txtype;
	const lr_write_t *lr = arg;
	const blkptr_t *bp = &lr->lr_blkptr;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);

	(void) printf("%s(encrypted)\n", tab_prefix);

	if (verbose < 4)
		return;

	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		(void) printf("%shas blkptr, %s\n", tab_prefix,
		    !BP_IS_HOLE(bp) && BP_GET_LOGICAL_BIRTH(bp) >=
		    spa_min_claim_txg(zilog->zl_spa) ?
		    "will claim" : "won't claim");
		print_log_bp(bp, tab_prefix);
	}
}

static void
zil_prt_rec_truncate(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_truncate_t *lr = arg;

	(void) printf("%sfoid %llu, offset 0x%llx, length 0x%llx\n", tab_prefix,
	    (u_longlong_t)lr->lr_foid, (longlong_t)lr->lr_offset,
	    (u_longlong_t)lr->lr_length);
}

static void
zil_prt_rec_setattr(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_setattr_t *lr = arg;
	time_t atime = (time_t)lr->lr_atime[0];
	time_t mtime = (time_t)lr->lr_mtime[0];

	(void) printf("%sfoid %llu, mask 0x%llx\n", tab_prefix,
	    (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_mask);

	if (lr->lr_mask & AT_MODE) {
		(void) printf("%sAT_MODE  %llo\n", tab_prefix,
		    (longlong_t)lr->lr_mode);
	}

	if (lr->lr_mask & AT_UID) {
		(void) printf("%sAT_UID   %llu\n", tab_prefix,
		    (u_longlong_t)lr->lr_uid);
	}

	if (lr->lr_mask & AT_GID) {
		(void) printf("%sAT_GID   %llu\n", tab_prefix,
		    (u_longlong_t)lr->lr_gid);
	}

	if (lr->lr_mask & AT_SIZE) {
		(void) printf("%sAT_SIZE  %llu\n", tab_prefix,
		    (u_longlong_t)lr->lr_size);
	}

	if (lr->lr_mask & AT_ATIME) {
		(void) printf("%sAT_ATIME %llu.%09llu %s", tab_prefix,
		    (u_longlong_t)lr->lr_atime[0],
		    (u_longlong_t)lr->lr_atime[1],
		    ctime(&atime));
	}

	if (lr->lr_mask & AT_MTIME) {
		(void) printf("%sAT_MTIME %llu.%09llu %s", tab_prefix,
		    (u_longlong_t)lr->lr_mtime[0],
		    (u_longlong_t)lr->lr_mtime[1],
		    ctime(&mtime));
	}
}

static void
zil_prt_rec_setsaxattr(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_setsaxattr_t *lr = arg;

	const char *name = (const char *)&lr->lr_data[0];
	(void) printf("%sfoid %llu\n", tab_prefix,
	    (u_longlong_t)lr->lr_foid);

	(void) printf("%sXAT_NAME  %s\n", tab_prefix, name);
	if (lr->lr_size == 0) {
		(void) printf("%sXAT_VALUE  NULL\n", tab_prefix);
	} else {
		(void) printf("%sXAT_VALUE  ", tab_prefix);
		const char *val = (const char *)&lr->lr_data[strlen(name) + 1];
		for (int i = 0; i < lr->lr_size; i++) {
			(void) printf("%c", *val);
			val++;
		}
	}
}

static void
zil_prt_rec_acl(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_acl_t *lr = arg;

	(void) printf("%sfoid %llu, aclcnt %llu\n", tab_prefix,
	    (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_aclcnt);
}

static void
zil_prt_rec_clone_range(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_clone_range_t *lr = arg;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);

	(void) printf("%sfoid %llu, offset %llx, length %llx, blksize %llx\n",
	    tab_prefix, (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_offset,
	    (u_longlong_t)lr->lr_length, (u_longlong_t)lr->lr_blksz);

	if (verbose < 4)
		return;

	for (unsigned int i = 0; i < lr->lr_nbps; i++) {
		(void) printf("%s[%u/%llu] ", tab_prefix, i + 1,
		    (u_longlong_t)lr->lr_nbps);
		print_log_bp(&lr->lr_bps[i], "");
	}
}

static void
zil_prt_rec_clone_range_enc(zilog_t *zilog, int txtype, const void *arg)
{
	(void) zilog, (void) txtype;
	const lr_clone_range_t *lr = arg;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);

	(void) printf("%s(encrypted)\n", tab_prefix);

	if (verbose < 4)
		return;

	for (unsigned int i = 0; i < lr->lr_nbps; i++) {
		(void) printf("%s[%u/%llu] ", tab_prefix, i + 1,
		    (u_longlong_t)lr->lr_nbps);
		print_log_bp(&lr->lr_bps[i], "");
	}
}

typedef void (*zil_prt_rec_func_t)(zilog_t *, int, const void *);
typedef struct zil_rec_info {
	zil_prt_rec_func_t	zri_print;
	zil_prt_rec_func_t	zri_print_enc;
	const char		*zri_name;
	uint64_t		zri_count;
} zil_rec_info_t;

static zil_rec_info_t zil_rec_info[TX_MAX_TYPE] = {
	{.zri_print = NULL,		    .zri_name = "Total              "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_CREATE          "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_MKDIR           "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_MKXATTR         "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_SYMLINK         "},
	{.zri_print = zil_prt_rec_remove,   .zri_name = "TX_REMOVE          "},
	{.zri_print = zil_prt_rec_remove,   .zri_name = "TX_RMDIR           "},
	{.zri_print = zil_prt_rec_link,	    .zri_name = "TX_LINK            "},
	{.zri_print = zil_prt_rec_rename,   .zri_name = "TX_RENAME          "},
	{.zri_print = zil_prt_rec_write,
	    .zri_print_enc = zil_prt_rec_write_enc,
	    .zri_name = "TX_WRITE           "},
	{.zri_print = zil_prt_rec_truncate, .zri_name = "TX_TRUNCATE        "},
	{.zri_print = zil_prt_rec_setattr,  .zri_name = "TX_SETATTR         "},
	{.zri_print = zil_prt_rec_acl,	    .zri_name = "TX_ACL_V0          "},
	{.zri_print = zil_prt_rec_acl,	    .zri_name = "TX_ACL_ACL         "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_CREATE_ACL      "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_CREATE_ATTR     "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_CREATE_ACL_ATTR "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_MKDIR_ACL       "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_MKDIR_ATTR      "},
	{.zri_print = zil_prt_rec_create,   .zri_name = "TX_MKDIR_ACL_ATTR  "},
	{.zri_print = zil_prt_rec_write,    .zri_name = "TX_WRITE2          "},
	{.zri_print = zil_prt_rec_setsaxattr,
	    .zri_name = "TX_SETSAXATTR      "},
	{.zri_print = zil_prt_rec_rename,   .zri_name = "TX_RENAME_EXCHANGE "},
	{.zri_print = zil_prt_rec_rename,   .zri_name = "TX_RENAME_WHITEOUT "},
	{.zri_print = zil_prt_rec_clone_range,
	    .zri_print_enc = zil_prt_rec_clone_range_enc,
	    .zri_name = "TX_CLONE_RANGE     "},
};

static int
print_log_record(zilog_t *zilog, const lr_t *lr, void *arg, uint64_t claim_txg)
{
	(void) arg, (void) claim_txg;
	int txtype;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);

	/* reduce size of txtype to strip off TX_CI bit */
	txtype = lr->lrc_txtype;

	ASSERT(txtype != 0 && (uint_t)txtype < TX_MAX_TYPE);
	ASSERT(lr->lrc_txg);

	(void) printf("\t\t%s%s len %6llu, txg %llu, seq %llu\n",
	    (lr->lrc_txtype & TX_CI) ? "CI-" : "",
	    zil_rec_info[txtype].zri_name,
	    (u_longlong_t)lr->lrc_reclen,
	    (u_longlong_t)lr->lrc_txg,
	    (u_longlong_t)lr->lrc_seq);

	if (txtype && verbose >= 3) {
		if (!zilog->zl_os->os_encrypted) {
			zil_rec_info[txtype].zri_print(zilog, txtype, lr);
		} else if (zil_rec_info[txtype].zri_print_enc) {
			zil_rec_info[txtype].zri_print_enc(zilog, txtype, lr);
		} else {
			(void) printf("%s(encrypted)\n", tab_prefix);
		}
	}

	zil_rec_info[txtype].zri_count++;
	zil_rec_info[0].zri_count++;

	return (0);
}

static int
print_log_block(zilog_t *zilog, const blkptr_t *bp, void *arg,
    uint64_t claim_txg)
{
	(void) arg;
	char blkbuf[BP_SPRINTF_LEN + 10];
	int verbose = MAX(dump_opt['d'], dump_opt['i']);
	const char *claim;

	if (verbose <= 3)
		return (0);

	if (verbose >= 5) {
		(void) strcpy(blkbuf, ", ");
		snprintf_blkptr(blkbuf + strlen(blkbuf),
		    sizeof (blkbuf) - strlen(blkbuf), bp);
	} else {
		blkbuf[0] = '\0';
	}

	if (claim_txg != 0)
		claim = "already claimed";
	else if (BP_GET_LOGICAL_BIRTH(bp) >= spa_min_claim_txg(zilog->zl_spa))
		claim = "will claim";
	else
		claim = "won't claim";

	(void) printf("\tBlock seqno %llu, %s%s\n",
	    (u_longlong_t)bp->blk_cksum.zc_word[ZIL_ZC_SEQ], claim, blkbuf);

	return (0);
}

static void
print_log_stats(int verbose)
{
	unsigned i, w, p10;

	if (verbose > 3)
		(void) printf("\n");

	if (zil_rec_info[0].zri_count == 0)
		return;

	for (w = 1, p10 = 10; zil_rec_info[0].zri_count >= p10; p10 *= 10)
		w++;

	for (i = 0; i < TX_MAX_TYPE; i++)
		if (zil_rec_info[i].zri_count || verbose >= 3)
			(void) printf("\t\t%s %*llu\n",
			    zil_rec_info[i].zri_name, w,
			    (u_longlong_t)zil_rec_info[i].zri_count);
	(void) printf("\n");
}

void
dump_intent_log(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);
	int i;

	if (BP_IS_HOLE(&zh->zh_log) || verbose < 1)
		return;

	(void) printf("\n    ZIL header: claim_txg %llu, "
	    "claim_blk_seq %llu, claim_lr_seq %llu",
	    (u_longlong_t)zh->zh_claim_txg,
	    (u_longlong_t)zh->zh_claim_blk_seq,
	    (u_longlong_t)zh->zh_claim_lr_seq);
	(void) printf(" replay_seq %llu, flags 0x%llx\n",
	    (u_longlong_t)zh->zh_replay_seq, (u_longlong_t)zh->zh_flags);

	for (i = 0; i < TX_MAX_TYPE; i++)
		zil_rec_info[i].zri_count = 0;

	/* see comment in zil_claim() or zil_check_log_chain() */
	if (zilog->zl_spa->spa_uberblock.ub_checkpoint_txg != 0 &&
	    zh->zh_claim_txg == 0)
		return;

	if (verbose >= 2) {
		(void) printf("\n");
		(void) zil_parse(zilog, print_log_block, print_log_record, NULL,
		    zh->zh_claim_txg, B_FALSE);
		print_log_stats(verbose);
	}
}
