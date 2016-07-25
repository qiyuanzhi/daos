/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * dsms: Module Definitions
 *
 * dsms is the DSM server module/library. It exports the DSM RPC handlers and
 * the DSM server API. This file contains the definitions expected by server;
 * the DSM server API methods are exported directly where they are defined as
 * extern functions.
 */

#include <daos_srv/daos_server.h>
#include <daos/rpc.h>
#include "dsm_rpc.h"
#include "dsms_internal.h"

static int
init(void)
{
	int rc;

	rc = dsms_storage_init();
	if (rc != 0)
		return rc;

	rc = dsms_module_pool_init();
	if (rc != 0)
		dsms_storage_fini();

	return rc;
}

static int
fini(void)
{
	dsms_conts_close();
	dsms_pools_close();
	dsms_module_pool_fini();
	dsms_storage_fini();
	return 0;
}

/* Note: the rpc input/output parameters is defined in daos_rpc */
static struct daos_rpc_handler dsms_handlers[] = {
	{
		.dr_opc		= DSM_POOL_CONNECT,
		.dr_hdlr	= dsms_hdlr_pool_connect
	}, {
		.dr_opc		= DSM_POOL_DISCONNECT,
		.dr_hdlr	= dsms_hdlr_pool_disconnect
	}, {
		.dr_opc		= DSM_CONT_CREATE,
		.dr_hdlr	= dsms_hdlr_cont_create
	}, {
		.dr_opc		= DSM_CONT_DESTROY,
		.dr_hdlr	= dsms_hdlr_cont_destroy
	}, {
		.dr_opc		= DSM_CONT_OPEN,
		.dr_hdlr	= dsms_hdlr_cont_open
	}, {
		.dr_opc		= DSM_CONT_CLOSE,
		.dr_hdlr	= dsms_hdlr_cont_close
	}, {
		.dr_opc		= DSM_CONT_EPOCH_QUERY,
		.dr_hdlr	= dsms_hdlr_cont_op
	}, {
		.dr_opc		= DSM_CONT_EPOCH_HOLD,
		.dr_hdlr	= dsms_hdlr_cont_op
	}, {
		.dr_opc		= DSM_CONT_EPOCH_COMMIT,
		.dr_hdlr	= dsms_hdlr_cont_op
	}, {
		.dr_opc		= DSM_TGT_OBJ_UPDATE,
		.dr_hdlr	= dsms_hdlr_object_rw,
	}, {
		.dr_opc		= DSM_TGT_OBJ_FETCH,
		.dr_hdlr	= dsms_hdlr_object_rw,
	}, {
		.dr_opc		= DSM_TGT_OBJ_ENUMERATE,
		.dr_hdlr	= dsms_hdlr_object_enumerate,
	}, {
		.dr_opc		= 0
	}
};

static void *
dsm_tls_init(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key)
{
	struct dsm_tls *tls;

	D_ALLOC_PTR(tls);
	if (tls == NULL)
		return NULL;

	DAOS_INIT_LIST_HEAD(&tls->dt_pool_list);
	DAOS_INIT_LIST_HEAD(&tls->dt_cont_list);
	return tls;
}

static void
dsm_tls_fini(const struct dss_thread_local_storage *dtls,
	     struct dss_module_key *key, void *data)
{
	struct dsm_tls *tls = data;

	D_ASSERT(daos_list_empty(&tls->dt_pool_list));
	D_FREE_PTR(tls);
}

struct dss_module_key dsm_module_key = {
	.dmk_tags = DAOS_SERVER_TAG,
	.dmk_index = -1,
	.dmk_init = dsm_tls_init,
	.dmk_fini = dsm_tls_fini,
};

struct dss_module daos_m_srv_module =  {
	.sm_name	= "daos_m_srv",
	.sm_mod_id	= DAOS_DSM_MODULE,
	.sm_ver		= 1,
	.sm_init	= init,
	.sm_fini	= fini,
	.sm_cl_rpcs	= dsm_rpcs,
	.sm_handlers	= dsms_handlers,
	.sm_key		= &dsm_module_key,
};
