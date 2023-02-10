/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2023 The Fluent Bit Authors
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


#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_lib.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_filter.h>
#include <fluent-bit/flb_output.h>
#include <fluent-bit/flb_custom.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_config_format.h>
#include <fluent-bit/flb_utils.h>

#include <cfl/cfl.h>
#include <cfl/cfl_sds.h>
#include <cfl/cfl_variant.h>
#include <cfl/cfl_kvlist.h>

static int flb_input_propery_check_all(struct flb_config *config)
{
    int ret;
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_input_instance *ins;
    struct flb_input_plugin *p;

    /* Iterate all active input instance plugins */
    mk_list_foreach_safe(head, tmp, &config->inputs) {
        ins = mk_list_entry(head, struct flb_input_instance, _head);
        p = ins->p;

        /* Skip pseudo input plugins */
        if (!p) {
            continue;
        }

        /* Check net property */
        ret = flb_input_net_property_check(ins, config);
        if (ret == -1) {
            return -1;
        }

        /* Check plugin property */
        ret = flb_input_plugin_property_check(ins, config);
        if (ret == -1) {
            return -1;
        }

        /* destroy net config map (will be recreated at flb_start) */
        if (ins->net_config_map) {
            flb_config_map_destroy(ins->net_config_map);
        }

        /* destroy config map (will be recreated at flb_start) */
        if (ins->config_map) {
            flb_config_map_destroy(ins->config_map);
        }
    }

    return 0;
}

static int flb_output_propery_check_all(struct flb_config *config)
{
    int ret;
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_output_instance *ins;

    /* Iterate all active input instance plugins */
    mk_list_foreach_safe(head, tmp, &config->outputs) {
        ins = mk_list_entry(head, struct flb_output_instance, _head);

        /* Check net property */
        ret = flb_output_net_property_check(ins, config);
        if (ret == -1) {
            return -1;
        }

        /* Check plugin property */
        ret = flb_output_plugin_property_check(ins, config);
        if (ret == -1) {
            return -1;
        }

        /* destroy net config map (will be recreated at flb_start) */
        if (ins->net_config_map) {
            flb_config_map_destroy(ins->net_config_map);
        }

        /* destroy config map (will be recreated at flb_start) */
        if (ins->config_map) {
            flb_config_map_destroy(ins->config_map);
        }
    }

    return 0;
}

static int flb_filter_propery_check_all(struct flb_config *config)
{
    int ret;
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_filter_instance *ins;

    /* Iterate all active input instance plugins */
    mk_list_foreach_safe(head, tmp, &config->filters) {
        ins = mk_list_entry(head, struct flb_filter_instance, _head);

        if (flb_filter_match_property_existence(ins) == FLB_FALSE) {
            flb_error("[filter] NO match rule for %s filter instance, halting to reload.",
                     ins->name);
            return -1;
        }

        /* Check plugin property */
        ret = flb_filter_plugin_property_check(ins, config);
        if (ret == -1) {
            return -1;
        }

        /* destroy config map (will be recreated at flb_start) */
        if (ins->config_map) {
            flb_config_map_destroy(ins->config_map);
        }
    }

    return 0;
}

static int flb_custom_propery_check_all(struct flb_config *config)
{
    int ret;
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_custom_instance *ins;

    /* Iterate all active input instance plugins */
    mk_list_foreach_safe(head, tmp, &config->customs) {
        ins = mk_list_entry(head, struct flb_custom_instance, _head);

        /* Check plugin property */
        ret = flb_custom_plugin_property_check(ins, config);
        if (ret == -1) {
            return -1;
        }

        /* destroy config map (will be recreated at flb_start) */
        if (ins->config_map) {
            flb_config_map_destroy(ins->config_map);
        }
    }

    return 0;
}

int flb_reload_property_check_all(struct flb_config *config)
{
    int ret = 0;

    /* Check properties of custom plugins */
    ret = flb_custom_propery_check_all(config);
    if (ret == -1) {
        flb_error("[reload] check properties for custom plugins is failed");

        return -1;
    }

    /* Check properties of input plugins */
    ret = flb_input_propery_check_all(config);
    if (ret == -1) {
        flb_error("[reload] check properties for input plugins is failed");

        return -1;
    }

    /* Check properties of filter plugins */
    ret = flb_filter_propery_check_all(config);
    if (ret == -1) {
        flb_error("[reload] check properties for filter plugins is failed");

        return -1;
    }

    /* Check properties of output plugins */
    ret = flb_output_propery_check_all(config);
    if (ret == -1) {
        flb_error("[reload] check properties for output plugins is failed");

        return -1;
    }

    return 0;
}

/*
 * Hot reload
 * ----------
 * Reload a Fluent Bit instance by using a new 'config_format' context.
 *
 *  1. As a first step, the config format is validated against the 'config maps',
 *     this will check that all configuration properties are valid.
 */

static int recreate_cf_section(struct flb_cf_section *s, struct flb_cf *cf)
{
    struct mk_list *head;
    struct cfl_list *p_head;
    struct cfl_kvpair *kv;
    struct flb_cf_group *g;
    struct flb_cf_section *new_s;
    struct flb_cf_group *new_g;
    struct cfl_variant *var = NULL;

    new_s = flb_cf_section_create(cf, s->name, flb_sds_len(s->name));
    if (cfl_list_size(&s->properties->list) > 0) {
        cfl_list_foreach(p_head, &s->properties->list) {
            var = NULL;
            kv = cfl_list_entry(p_head, struct cfl_kvpair, _head);
            var = flb_cf_section_property_add(cf, new_s->properties,
                                              kv->key, cfl_sds_len(kv->key),
                                              kv->val->data.as_string, cfl_sds_len(kv->val->data.as_string));

            if (var == NULL) {
                flb_error("[reload] recreating section '%s' property '%s' is failed", s->name, kv->key);
                return -1;
            }
        }
    }

    if (mk_list_size(&s->groups) <= 0) {
        return 0;
    }

    mk_list_foreach(head, &s->groups) {
        g = mk_list_entry(head, struct flb_cf_group, _head);
        new_g = flb_cf_group_create(cf, new_s, g->name, flb_sds_len(g->name));

        if (cfl_list_size(&g->properties->list) > 0) {
            cfl_list_foreach(p_head, &g->properties->list) {
                var = NULL;
                kv = cfl_list_entry(p_head, struct cfl_kvpair, _head);
                var = flb_cf_section_property_add(cf, new_g->properties,
                                                  kv->key, cfl_sds_len(kv->key),
                                                  kv->val->data.as_string, cfl_sds_len(kv->val->data.as_string));
                if (var == NULL) {
                    flb_error("[reload] recreating group '%s' property '%s' is failed", g->name, kv->key);
                    return -1;
                }
            }
        }
    }

    return 0;
}

int flb_reload_reconstruct_cf(struct flb_cf *src_cf, struct flb_cf *dest_cf)
{
    struct mk_list *head;
    struct flb_cf_section *s;

    mk_list_foreach(head, &src_cf->sections) {
        s = mk_list_entry(head, struct flb_cf_section, _head);
        if (recreate_cf_section(s, dest_cf) != 0) {
            return -1;
        }
    }

    return 0;
}

int flb_reload(flb_ctx_t *ctx, struct flb_cf *cf_opts)
{
    int ret;
    flb_sds_t file = NULL;
    struct flb_config *old_config = ctx->config;
    struct flb_config *new_config;
    flb_ctx_t *new_ctx;
    struct flb_cf *new_cf;
    struct flb_cf *original_cf;

    /* Normally, we should create a service section before using this cf
     * context. However, this context of config format will be used
     * for copying contents from other one. So, we just need to create
     * a new cf instance here.
     */
    new_cf = flb_cf_create();
    if (!new_cf) {
        return -1;
    }

    flb_info("reloading instance pid=%lu tid=%i", getpid(), pthread_self());

    printf("[PRE STOP DUMP]\n");
    flb_cf_dump(old_config->cf_main);

    if (old_config->conf_path_file) {
        file = flb_sds_create(old_config->conf_path_file);
    }
    if (cf_opts != NULL) {
        if (flb_reload_reconstruct_cf(cf_opts, new_cf) != 0) {
            flb_error("[reload] reconstruct cf failed");
            return -1;
        }
    }

    /* Create another instance */
    new_ctx = flb_create();

    new_config = new_ctx->config;

    /* Delete the original context of config format */
    original_cf = new_config->cf_main;
    flb_cf_destroy(original_cf);

    /* Create another config format context */
    if (file != NULL) {
        new_cf = flb_cf_create_from_file(new_cf, file);

        if (!new_cf) {
            flb_sds_destroy(file);

            return -1;
        }
    }

    /* Load the new config format context to config context. */
    ret = flb_config_load_config_format(new_config, new_cf);
    if (ret != 0) {
        flb_sds_destroy(file);
        flb_cf_destroy(new_cf);
        flb_stop(new_ctx);
        flb_destroy(new_ctx);

        flb_error("[reload] reloaded config format is invalid. Reloading is halted");

        return -1;
    }

    /* Validate plugin properites before fluent-bit stops the old context. */
    ret = flb_reload_property_check_all(new_config);
    if (ret != 0) {
        flb_sds_destroy(file);
        flb_cf_destroy(new_cf);
        flb_stop(new_ctx);
        flb_destroy(new_ctx);

        flb_error("[reload] reloaded config is invalid. Reloading is halted");

        return -1;
    }

    new_config->cf_main = new_cf;

    if (file != NULL) {
        new_config->conf_path_file = file;
    }

    flb_info("[reload] stop everything of the old context");
    flb_stop(ctx);
    flb_destroy(ctx);

    /* FIXME: DEBUG */
    printf("[POS STOP DUMP]\n");
    flb_cf_dump(new_config->cf_main);
    flb_info("[reload] start everything");

    ret = flb_start(new_ctx);


    printf("reload new_ctx flb_start() => %i\n", ret);

    return 0;
}
