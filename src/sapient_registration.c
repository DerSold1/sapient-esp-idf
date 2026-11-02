/* SPDX-License-Identifier: Apache-2.0
 *
 * Simplified Registration builder. Registration's top-level lists are
 * FT_CALLBACK fields (see proto/sapient.options) so that SapientMessage stays
 * small; here we provide the encode callbacks that fill them from a
 * sapient_registration_cfg_t.
 */
#include <stdlib.h>
#include <string.h>

#include "pb_encode.h"
#include "sapient.h"

#define P(x) sapient_msg_bsi_flex_335_v2_0_##x

typedef struct {
    const sapient_registration_cfg_t *cfg;
    /* scratch structs (heap, too big for a task stack) */
    P(Registration_ModeDefinition) mode;
    P(Registration_StatusDefinition) status;
    P(Registration_NodeDefinition) node;
    P(Registration_ConfigurationData) config;
    P(Registration_Capability) cap;
} reg_ctx_t;

static void set_duration_s(P(Registration_Duration) *d, float seconds)
{
    d->has_units = true;
    d->units = P(Registration_TimeUnits_TIME_UNITS_SECONDS);
    d->has_value = true;
    d->value = seconds;
}

static void set_location_type(P(Registration_LocationType) *lt, sapient_report_location_t kind)
{
    memset(lt, 0, sizeof(*lt));
    if (kind == SAPIENT_REPORT_RANGE_BEARING) {
        lt->which_coordinates_oneof = P(Registration_LocationType_range_bearing_units_tag);
        lt->coordinates_oneof.range_bearing_units =
                P(RangeBearingCoordinateSystem_RANGE_BEARING_COORDINATE_SYSTEM_DEGREES_M);
        lt->which_datum_oneof = P(Registration_LocationType_range_bearing_datum_tag);
        lt->datum_oneof.range_bearing_datum = P(RangeBearingDatum_RANGE_BEARING_DATUM_TRUE);
    } else {
        lt->which_coordinates_oneof = P(Registration_LocationType_location_units_tag);
        lt->coordinates_oneof.location_units = P(LocationCoordinateSystem_LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
        lt->which_datum_oneof = P(Registration_LocationType_location_datum_tag);
        lt->datum_oneof.location_datum = P(LocationDatum_LOCATION_DATUM_WGS84_G);
    }
}

/* ---------------------------------------------------------------- callbacks */

static bool enc_node_definition(pb_ostream_t *s, const pb_field_t *f, void *const *arg)
{
    reg_ctx_t *c = (reg_ctx_t *)*arg;
    memset(&c->node, 0, sizeof(c->node));
    c->node.has_node_type = true;
    c->node.node_type = c->cfg->node_type;
    if (c->cfg->node_sub_type) {
        c->node.node_sub_type_count = 1;
        SAPIENT_STRCPY(c->node.node_sub_type[0], c->cfg->node_sub_type);
    }
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, P(Registration_NodeDefinition_fields), &c->node);
}

static bool enc_capabilities(pb_ostream_t *s, const pb_field_t *f, void *const *arg)
{
    reg_ctx_t *c = (reg_ctx_t *)*arg;
    for (size_t i = 0; i < c->cfg->capabilities_count; i++) {
        const sapient_capability_t *in = &c->cfg->capabilities[i];
        memset(&c->cap, 0, sizeof(c->cap));
        c->cap.has_category = true;
        SAPIENT_STRCPY(c->cap.category, in->category);
        c->cap.has_type = true;
        SAPIENT_STRCPY(c->cap.type, in->type);
        if (in->value) {
            c->cap.has_value = true;
            SAPIENT_STRCPY(c->cap.value, in->value);
        }
        if (in->units) {
            c->cap.has_units = true;
            SAPIENT_STRCPY(c->cap.units, in->units);
        }
        if (!pb_encode_tag_for_field(s, f) ||
                !pb_encode_submessage(s, P(Registration_Capability_fields), &c->cap)) {
            return false;
        }
    }
    return true;
}

static bool enc_status_definition(pb_ostream_t *s, const pb_field_t *f, void *const *arg)
{
    reg_ctx_t *c = (reg_ctx_t *)*arg;
    memset(&c->status, 0, sizeof(c->status));
    c->status.has_status_interval = true;
    set_duration_s(&c->status.status_interval, c->cfg->status_interval_s > 0 ? c->cfg->status_interval_s : 10.0f);
    if (c->cfg->reports_node_location) {
        c->status.has_location_definition = true;
        set_location_type(&c->status.location_definition, SAPIENT_REPORT_LOCATION_WGS84);
    }
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, P(Registration_StatusDefinition_fields), &c->status);
}

static bool enc_mode_definition(pb_ostream_t *s, const pb_field_t *f, void *const *arg)
{
    reg_ctx_t *c = (reg_ctx_t *)*arg;
    const sapient_registration_cfg_t *cfg = c->cfg;
    P(Registration_ModeDefinition) *m = &c->mode;
    memset(m, 0, sizeof(*m));

    m->has_mode_name = true;
    SAPIENT_STRCPY(m->mode_name, cfg->mode_name ? cfg->mode_name : "Default");
    m->has_mode_type = true;
    m->mode_type = P(Registration_ModeType_MODE_TYPE_DEFAULT);
    m->has_settle_time = true;
    set_duration_s(&m->settle_time, 0.0f);
    if (cfg->maximum_latency_s > 0) {
        m->has_maximum_latency = true;
        set_duration_s(&m->maximum_latency, cfg->maximum_latency_s);
    }
    m->has_scan_type = true;
    m->scan_type = cfg->scan_type ? cfg->scan_type : P(Registration_ScanType_SCAN_TYPE_FIXED);
    m->has_tracking_type = true;
    m->tracking_type = cfg->tracking_type ? cfg->tracking_type : P(Registration_TrackingType_TRACKING_TYPE_NONE);

    /* detection definition */
    m->detection_definition_count = 1;
    P(Registration_DetectionDefinition) *dd = &m->detection_definition[0];
    dd->has_location_type = true;
    set_location_type(&dd->location_type, cfg->detection_location);

    if (cfg->classes_count > 0) {
        const size_t max = sizeof(dd->detection_class_definition[0].class_definition) /
                           sizeof(dd->detection_class_definition[0].class_definition[0]);
        P(Registration_DetectionClassDefinition) *cd = &dd->detection_class_definition[0];
        dd->detection_class_definition_count = 1;
        cd->has_confidence_definition = true;
        cd->confidence_definition = cfg->classes_count > 1 ?
                P(Registration_ConfidenceDefinition_CONFIDENCE_DEFINITION_MULTI_CLASS) :
                P(Registration_ConfidenceDefinition_CONFIDENCE_DEFINITION_SINGLE_CLASS);
        for (size_t i = 0; i < cfg->classes_count && i < max; i++) {
            P(Registration_ClassDefinition) *cl = &cd->class_definition[cd->class_definition_count++];
            cl->has_type = true;
            SAPIENT_STRCPY(cl->type, cfg->classes[i]);
            cl->has_units = true;
            SAPIENT_STRCPY(cl->units, "Probability");
        }
    }

    /* task definition (mandatory): regions of interest / ignore in the same frame */
    m->has_task = true;
    m->task.has_region_definition = true;
    P(Registration_RegionDefinition) *rd = &m->task.region_definition;
    rd->region_type_count = 2;
    rd->region_type[0] = P(Registration_RegionType_REGION_TYPE_AREA_OF_INTEREST);
    rd->region_type[1] = P(Registration_RegionType_REGION_TYPE_IGNORE);
    rd->region_area_count = 1;
    set_location_type(&rd->region_area[0], cfg->detection_location);

    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, P(Registration_ModeDefinition_fields), m);
}

static bool enc_config_data(pb_ostream_t *s, const pb_field_t *f, void *const *arg)
{
    reg_ctx_t *c = (reg_ctx_t *)*arg;
    const sapient_registration_cfg_t *cfg = c->cfg;
    memset(&c->config, 0, sizeof(c->config));
    SAPIENT_STRCPY(c->config.manufacturer, cfg->manufacturer ? cfg->manufacturer : "unknown");
    SAPIENT_STRCPY(c->config.model, cfg->model ? cfg->model : "unknown");
    if (cfg->serial_number) {
        c->config.has_serial_number = true;
        SAPIENT_STRCPY(c->config.serial_number, cfg->serial_number);
    }
    if (cfg->hardware_version) {
        c->config.has_hardware_version = true;
        SAPIENT_STRCPY(c->config.hardware_version, cfg->hardware_version);
    }
    if (cfg->software_version) {
        c->config.has_software_version = true;
        SAPIENT_STRCPY(c->config.software_version, cfg->software_version);
    }
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, P(Registration_ConfigurationData_fields), &c->config);
}

/* -------------------------------------------------------------------- API */

sapient_err_t sapient_encode_registration_frame(const char *node_id, const sapient_registration_cfg_t *cfg,
        uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!node_id || !cfg || !buf || !out_len) {
        return SAPIENT_ERR_ARG;
    }

    reg_ctx_t *ctx = (reg_ctx_t *)calloc(1, sizeof(reg_ctx_t));
    sapient_message_t *msg = (sapient_message_t *)calloc(1, sizeof(sapient_message_t));
    if (!ctx || !msg) {
        free(ctx);
        free(msg);
        return SAPIENT_ERR_NO_MEM;
    }
    ctx->cfg = cfg;

    sapient_message_init(msg, node_id, NULL);
    msg->which_content = SAPIENT_CONTENT_REGISTRATION;
    sapient_registration_t *r = &msg->content.registration;
    r->has_icd_version = true;
    SAPIENT_STRCPY(r->icd_version, SAPIENT_ICD_VERSION);
    if (cfg->name) {
        r->has_name = true;
        SAPIENT_STRCPY(r->name, cfg->name);
    }
    if (cfg->short_name) {
        r->has_short_name = true;
        SAPIENT_STRCPY(r->short_name, cfg->short_name);
    }
    r->node_definition.funcs.encode = enc_node_definition;
    r->node_definition.arg = ctx;
    r->capabilities.funcs.encode = enc_capabilities;
    r->capabilities.arg = ctx;
    r->status_definition.funcs.encode = enc_status_definition;
    r->status_definition.arg = ctx;
    r->mode_definition.funcs.encode = enc_mode_definition;
    r->mode_definition.arg = ctx;
    r->config_data.funcs.encode = enc_config_data;
    r->config_data.arg = ctx;

    sapient_err_t err = sapient_encode_frame(msg, buf, cap, out_len);
    free(msg);
    free(ctx);
    return err;
}
