/*
 * Copyright (c) 2008, XenSource Inc.
 * Copyright (c) 2010, Citrix Systems, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of XenSource Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "libvhd.h"

TEST_FAIL_EXTERN_VARS;

static int
vhd_util_zero_bat(vhd_context_t *vhd)
{
	int err, map_bytes;
	uint64_t i;

	err = vhd_get_bat(vhd);
	if (err)
		return err;

	if (vhd_has_batmap(vhd)) {
		err = vhd_get_batmap(vhd);
		if (err)
			return err;
	}

	for (i = 0; i < vhd->header.max_bat_size; i++)
		vhd->bat.bat[i] = DD_BLK_UNUSED;
	err = vhd_write_bat(vhd, &vhd->bat);
	if (err)
		return err;

	map_bytes = vhd_sectors_to_bytes(vhd->batmap.header.batmap_size);
	memset(vhd->batmap.map, 0, map_bytes);
	return vhd_write_batmap(vhd, &vhd->batmap);
}

int
vhd_util_modify(int argc, char **argv)
{
	char *name, *type;
	vhd_context_t vhd;
	int err, c, size, parent, parent_raw, kill_data;
	off64_t newsize = 0;
	char *newparent = NULL;

	name       = NULL;
	type       = NULL;
	size       = 0;
	parent     = 0;
	parent_raw = 0;
	kill_data  = 0;

	optind = 0;
	while ((c = getopt(argc, argv, "n:s:p:mt:zh")) != -1) {
		switch (c) {
		case 'n':
			name = optarg;
			break;
		case 's':
			size = 1;
			errno = 0;
			newsize = strtoll(optarg, NULL, 10);
			if (errno) {
				fprintf(stderr, "Invalid size '%s'\n", optarg);
				goto usage;
			}
			break;
		case 'p':
			parent = 1;
			newparent = optarg;
			break;
		case 'm':
			parent_raw = 1;
			break;
		case 't':
			type = optarg;
			break;
		case 'z':
			kill_data = 1;
			break;
		case 'h':
		default:
			goto usage;
		}
	}

	if (!name || optind != argc)
		goto usage;

	err = vhd_open(&vhd, name, VHD_OPEN_RDWR);
	if (err) {
		printf("error opening %s: %d\n", name, err);
		return err;
	}

	if (kill_data) {
		if (vhd_type_dynamic(&vhd))
			err = vhd_util_zero_bat(&vhd);
		else
			err = -ENOSYS;

		if (!err) {
			err = vhd_end_of_headers(&vhd, &newsize);
			newsize += sizeof(vhd_footer_t);
		}

		if (!err)
			err = vhd_set_phys_size(&vhd, newsize);

		if (err)
			printf("failed to zero VHD: %d\n", err);
	}

	if (size) {
		err = vhd_set_phys_size(&vhd, newsize);
		if (err)
			printf("failed to set physical size to %"PRIu64":"
			       " %d\n", newsize, err);
	}

	if (parent) {
		TEST_FAIL_AT(FAIL_REPARENT_BEGIN);
		err = vhd_change_parent(&vhd, newparent, parent_raw);
		if (err) {
			printf("failed to set parent to '%s': %d\n",
					newparent, err);
			goto done;
		}
		TEST_FAIL_AT(FAIL_REPARENT_END);
	}

	if (type) {
		if (vhd.footer.type == HD_TYPE_FIXED) {
			printf("changing fixed vhd type not supported\n");
			err = -EINVAL;
			goto done;
		}

		if (!strcmp(type, "differencing"))
			vhd.footer.type = HD_TYPE_DIFF;
		else if (!strcmp(type, "dynamic"))
			vhd.footer.type = HD_TYPE_DYNAMIC;
		else if (!strcmp(type, "original")) {
			char *n;

			err = vhd_header_decode_parent(&vhd, &vhd.header, &n);
			if (err) {
				printf("error inferring type: %d\n", err);
				goto done;
			}

			if (!strcmp(n, ""))
				vhd.footer.type = HD_TYPE_DYNAMIC;
			else
				vhd.footer.type = HD_TYPE_DIFF;

			free(n);
		} else {
			printf("invalid type %s (must be 'differencing or "
			       "dynamic')\n", type);
			err = -EINVAL;
			goto done;
		}
		err = vhd_write_footer(&vhd, &vhd.footer);
	}

done:
	vhd_close(&vhd);
	return err;

usage:
	printf("*** Dangerous operations, use with care ***\n");
	printf("options: <-n name> [-p NEW_PARENT set parent [-m raw]] "
	       "[-s NEW_SIZE set size] [-z zero (kill data)] "
	       "[-t TYPE (differencing|dynamic|original)] [-h help]\n");
	return -EINVAL;
}
