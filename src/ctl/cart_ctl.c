/* Copyright (C) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of CaRT. It implements client side of the cart_ctl command
 * line utility.
 */
#define D_LOGFAC	DD_FAC(ctl)

#include "crt_internal.h"
#include <gurt/common.h>
#include <cart/api.h>

#include <stdio.h>
#include <pthread.h>
#include <getopt.h>
#include <semaphore.h>

/* max number of ranks that can be queried at once */
#define CRT_CTL_MAX 1024
#define CRT_CTL_MAX_ARG_STR_LEN (1 << 16)


int crt_ctl_logfac;

struct ctl_g {
	int		 cg_cmd_code;
	char		*cg_group_name;
	crt_group_t	*cg_target_group;
	int		 cg_num_ranks;
	d_rank_t	 cg_ranks[CRT_CTL_MAX];
	crt_context_t	 cg_crt_ctx;
	pthread_t	 cg_tid;
	int		 cg_complete;
	sem_t		 cg_num_reply;
};

static struct ctl_g ctl_gdata;

static void *
progress_thread(void *arg)
{
	int			rc;
	crt_context_t		crt_ctx;

	crt_ctx = (crt_context_t) arg;
	/* progress loop */
	do {
		rc = crt_progress(crt_ctx, 1, NULL, NULL);
		if (rc != 0 && rc != -DER_TIMEDOUT) {
			D_ERROR("crt_progress failed rc: %d.\n", rc);
			break;
		}


		if (ctl_gdata.cg_complete == 1) {
			D_DEBUG(DB_TRACE, "ctl_gdata.cg_complete %d.\n",
				ctl_gdata.cg_complete);
			break;
		}
	} while (1);

	printf("progress_thread: progress thread exit ...\n");

	pthread_exit(NULL);
}

static void
parse_rank_string(char *arg_str, d_rank_t *ranks, int *num_ranks)
{
	char		*token;
	char		*ptr;
	uint32_t	 num_ranks_l = 0;
	uint32_t	 index = 0;
	int		 rstart;
	int		 rend;
	int		 i;

	D_ASSERT(ranks != NULL);
	D_ASSERT(num_ranks != NULL);
	D_ASSERT(arg_str != NULL);
	if (strnlen(arg_str, CRT_CTL_MAX_ARG_STR_LEN) >=
		    CRT_CTL_MAX_ARG_STR_LEN) {
		D_ERROR("arg string too long.\n");
		return;
	}
	D_DEBUG(DB_TRACE, "arg_str %s\n", arg_str);
	token = strtok(arg_str, ",");
	while (token != NULL) {
		ptr = strchr(token, '-');
		if (ptr == NULL) {
			num_ranks_l++;
			if (num_ranks_l > CRT_CTL_MAX) {
				D_ERROR("Too many target ranks.\n");
				return;
			}
			ranks[index] = atoi(token);
			index++;
			token = strtok(NULL, ",");
			continue;
		}
		if (ptr == token || ptr == token + strlen(token)) {
			D_ERROR("Invalid rank range.\n");
			return;
		}
		rstart = atoi(token);
		rend = atoi(ptr + 1);
		num_ranks_l += (rend - rstart + 1);
		if (num_ranks_l > CRT_CTL_MAX) {
			D_ERROR("Too many target ranks.\n");
			return;
		}
		for (i = rstart; i < rend + 1; i++) {
			ranks[index] = i;
			index++;
		}
		token = strtok(NULL, ",");
	}
	*num_ranks = num_ranks_l;

	fprintf(stdout, "requested %d target ranks: ", num_ranks_l);
	for (i = 0; i < num_ranks_l; i++)
		fprintf(stdout, " %d", ranks[i]);
	fprintf(stdout, "\n");
}

static void
print_usage_msg(void)
{
	printf("Usage: cart_ctl list_ctx --group-name name --rank "
	       "start-end,start-end,rank,rank\n");
	printf("\nThis command takes a group name and a list of ranks as "
		"arguments, it will pint the number of CART contexts on each "
		"specified rank and the URI of each context.\n");
}

static int
parse_args(int argc, char **argv)
{
	int		option_index = 0;
	int		opt;
	int		rc = 0;

	if (argc <= 2) {
		print_usage_msg();
		D_GOTO(out, rc = -DER_INVAL);
	}
	if (strcmp(argv[1], "list_ctx") == 0)
		ctl_gdata.cg_cmd_code = 0;

	optind = 2;
	while (1) {
		static struct option long_options[] = {
			{"group-name", required_argument, 0, 'g'},
			{"rank", required_argument, 0, 'r'},
			{0, 0, 0, 0},
		};

		opt = getopt_long(argc, argv, "g:r:", long_options, NULL);
		if (opt == -1)
			break;
		switch (opt) {
		case 0:
			if (long_options[option_index].flag != 0)
				break;
		case 'g':
			ctl_gdata.cg_group_name = optarg;
			break;
		case 'r':
			parse_rank_string(optarg, ctl_gdata.cg_ranks,
					  &ctl_gdata.cg_num_ranks);
		}
	}

out:
	return rc;
}

static void
ctl_client_cb(const struct crt_cb_info *cb_info)
{
	struct crt_ctl_ep_ls_in		*in_args;
	struct crt_ctl_ep_ls_out	*out_args;
	char				*addr_str;
	int				 i;

	in_args = crt_req_get(cb_info->cci_rpc);
	out_args = crt_reply_get(cb_info->cci_rpc);
	fprintf(stdout, "group: %s, rank %d, ctx_num %d\n",
		in_args->cel_grp_id, in_args->cel_rank, out_args->cel_ctx_num);
	addr_str = out_args->cel_addr_str.iov_buf;
	for (i = 0; i < out_args->cel_ctx_num; i++) {
		fprintf(stdout, "    %s\n", addr_str);
		addr_str += (strlen(addr_str) + 1);
	}

	sem_post(&ctl_gdata.cg_num_reply);
}

int
ctl_ls_ctx()
{
	int				 i;
	crt_rpc_t			*rpc_req;
	struct crt_ctl_ep_ls_in		*in_args;
	crt_endpoint_t			 ep;
	int				 rc = 0;

	D_DEBUG(DB_TRACE, "num requested ranks %d\n", ctl_gdata.cg_num_ranks);
	for (i = 0; i < ctl_gdata.cg_num_ranks; i++) {
		ep.ep_grp = ctl_gdata.cg_target_group;
		ep.ep_rank = ctl_gdata.cg_ranks[i];
		ep.ep_tag = 0;
		rc = crt_req_create(ctl_gdata.cg_crt_ctx, &ep,
				    CRT_OPC_CTL_LS, &rpc_req);
		if (rc != 0) {
			D_ERROR("crt_req_create() failed. rc %d.\n", rc);
			D_GOTO(out, rc);
		}
		in_args = crt_req_get(rpc_req);
		in_args->cel_grp_id = ctl_gdata.cg_target_group->cg_grpid;
		in_args->cel_rank = ctl_gdata.cg_ranks[i];
		D_DEBUG(DB_NET, "rpc_req %p rank %d tag %d seq %d\n",
			rpc_req, ep.ep_rank, ep.ep_tag, i);
		rc = crt_req_send(rpc_req, ctl_client_cb, NULL);
		if (rc != 0) {
			D_ERROR("crt_req_send() failed. rpc_req %p rank %d tag "
				"%d rc %d.\n",
				rpc_req, ep.ep_rank, ep.ep_tag, rc);
			D_GOTO(out, rc);
		}
	}
	for (i = 0; i < ctl_gdata.cg_num_ranks; i++)
		sem_wait(&ctl_gdata.cg_num_reply);

out:
	return rc;
}

int
exec_cmd()
{
	int	rc = 0;

	switch (ctl_gdata.cg_cmd_code) {
	case 0:
		/* list_ctx */
		rc = ctl_ls_ctx();
		if (rc != 0) {
			D_ERROR("ctl_ls_ctx() failed. rc %d", rc);
			D_GOTO(out, rc);
		}
		break;
	}

out:
	return rc;
}

int
ctl_init()
{
	int rc;

	rc = crt_init("crt_ctl", 0);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	rc = d_log_init();
	D_ASSERTF(rc == 0, "d_log_init() failed. rc: %d\n", rc);

	rc = crt_context_create(&ctl_gdata.cg_crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);

	ctl_gdata.cg_complete = 0;
	rc = sem_init(&ctl_gdata.cg_num_reply, 0, 0);
	D_ASSERTF(rc == 0, "Could not initialize semaphore. rc %d\n", rc);
	rc = pthread_create(&ctl_gdata.cg_tid, NULL, progress_thread,
			    ctl_gdata.cg_crt_ctx);
	D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);

	rc = crt_group_attach(ctl_gdata.cg_group_name,
			      &ctl_gdata.cg_target_group);
	D_ASSERTF(rc == 0, "crt_group_attach failed, tgt_group: %s rc: %d\n",
		  ctl_gdata.cg_group_name, rc);
	D_ASSERTF(ctl_gdata.cg_target_group != NULL,
		  "NULL attached target_group\n");

	return rc;
}

int
ctl_finalize()
{
	int		rc;

	rc = crt_group_detach(ctl_gdata.cg_target_group);
	D_ASSERTF(rc == 0, "crt_group_detach failed, rc: %d\n", rc);
	ctl_gdata.cg_complete = 1;
	rc = pthread_join(ctl_gdata.cg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	rc = crt_context_destroy(ctl_gdata.cg_crt_ctx, 0);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);
	d_log_fini();
	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);

	return rc;
}

int
main(int argc, char **argv)
{
	int		rc = 0;

	rc = parse_args(argc, argv);
	if (rc != 0) {
		D_ERROR("parse_args() failed. rc %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = ctl_init();
	if (rc != 0) {
		D_ERROR("ctl_init() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = exec_cmd();
	if (rc != 0) {
		D_ERROR("exec_cmd() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}
	rc = ctl_finalize();
	if (rc != 0) {
		D_ERROR("ctl_finalize() failed, rc %d\n", rc);
		D_GOTO(out, rc);
	}

	fprintf(stderr, "cart_ctl exiting.\n");
out:
	return rc;
}
