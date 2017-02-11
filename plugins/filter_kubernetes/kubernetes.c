/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2017 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_filter.h>
#include <fluent-bit/flb_utils.h>

#include "kube_conf.h"
#include "kube_meta.h"

#include <stdio.h>
#include <msgpack.h>

static int cb_kube_init(struct flb_filter_instance *f_ins,
                        struct flb_config *config,
                        void *data)
{
    (void) f_ins;
    (void) config;
    (void) data;
    struct flb_kube *ctx;

    /* Create configuration context */
    ctx = flb_kube_conf_create(f_ins, config);
    if (!ctx) {
        return -1;
    }

    /* Set context */
    flb_filter_set_context(f_ins, ctx);

    /*
     * Get Kubernetes Metadata: we gather this at the beginning
     * as we need this information to process logs in Kubernetes
     * environment, otherwise the service should not start.
     */
    flb_kube_meta_fetch(ctx);

    return 0;
}

static int cb_kube_filter(void *data, size_t bytes,
                          char *tag, int tag_len,
                          void **out_buf, size_t *out_bytes,
                          struct flb_filter_instance *f_ins,
                          void *filter_context,
                          struct flb_config *config)
{
    msgpack_unpacked result;
    size_t off = 0, cnt = 0;
    (void) out_buf;
    (void) out_bytes;
    (void) f_ins;
    (void) filter_context;
    (void) config;

    msgpack_unpacked_init(&result);
    while (msgpack_unpack_next(&result, data, bytes, &off)) {
        printf("[%zd] %s: ", cnt++, tag);
        msgpack_object_print(stdout, result.data);
        printf("\n");
    }
    msgpack_unpacked_destroy(&result);

    return FLB_FILTER_NOTOUCH;
}

struct flb_filter_plugin filter_kubernetes_plugin = {
    .name         = "kubernetes",
    .description  = "Filter to append Kubernetes metadata",
    .cb_init      = cb_kube_init,
    .cb_filter    = cb_kube_filter,
    .cb_exit      = NULL,
    .flags        = 0
};
