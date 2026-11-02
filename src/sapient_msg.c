/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "sapient.h"

#define P(x) sapient_msg_bsi_flex_335_v2_0_##x

void sapient_message_init(sapient_message_t *msg, const char *node_id, const char *destination_id)
{
    memset(msg, 0, sizeof(*msg));
    uint64_t now = sapient_now_ms();
    msg->has_timestamp = true;
    msg->timestamp.seconds = (int64_t)(now / 1000u);
    msg->timestamp.nanos = (int32_t)((now % 1000u) * 1000000u);
    msg->has_node_id = true;
    SAPIENT_STRCPY(msg->node_id, node_id);
    if (destination_id) {
        msg->has_destination_id = true;
        SAPIENT_STRCPY(msg->destination_id, destination_id);
    }
}

/* ----------------------------------------------------------- content types */

sapient_status_report_t *sapient_message_status_report(sapient_message_t *msg,
        P(StatusReport_System) system, P(StatusReport_Info) info)
{
    msg->which_content = SAPIENT_CONTENT_STATUS_REPORT;
    sapient_status_report_t *sr = &msg->content.status_report;
    memset(sr, 0, sizeof(*sr));
    sr->has_report_id = true;
    sapient_ulid(sr->report_id);
    sr->has_system = true;
    sr->system = system;
    sr->has_info = true;
    sr->info = info;
    return sr;
}

sapient_detection_report_t *sapient_message_detection_report(sapient_message_t *msg, const char *object_id)
{
    msg->which_content = SAPIENT_CONTENT_DETECTION_REPORT;
    sapient_detection_report_t *dr = &msg->content.detection_report;
    memset(dr, 0, sizeof(*dr));
    dr->has_report_id = true;
    sapient_ulid(dr->report_id);
    dr->has_object_id = true;
    if (object_id) {
        SAPIENT_STRCPY(dr->object_id, object_id);
    } else {
        sapient_ulid(dr->object_id);
    }
    return dr;
}

sapient_alert_t *sapient_message_alert(sapient_message_t *msg, P(Alert_AlertType) type, const char *description)
{
    msg->which_content = SAPIENT_CONTENT_ALERT;
    sapient_alert_t *a = &msg->content.alert;
    memset(a, 0, sizeof(*a));
    a->has_alert_id = true;
    sapient_ulid(a->alert_id);
    a->has_alert_type = true;
    a->alert_type = type;
    a->has_status = true;
    a->status = P(Alert_AlertStatus_ALERT_STATUS_ACTIVE);
    if (description) {
        a->has_description = true;
        SAPIENT_STRCPY(a->description, description);
    }
    return a;
}

sapient_task_ack_t *sapient_message_task_ack(sapient_message_t *msg, const char *task_id,
        P(TaskAck_TaskStatus) status, const char *reason)
{
    msg->which_content = SAPIENT_CONTENT_TASK_ACK;
    sapient_task_ack_t *ack = &msg->content.task_ack;
    memset(ack, 0, sizeof(*ack));
    ack->has_task_id = true;
    SAPIENT_STRCPY(ack->task_id, task_id);
    ack->has_task_status = true;
    ack->task_status = status;
    if (reason) {
        ack->reason_count = 1;
        SAPIENT_STRCPY(ack->reason[0], reason);
    }
    return ack;
}

/* ---------------------------------------------------------------- location */

void sapient_location_wgs84(sapient_location_t *loc, double lat_deg, double lon_deg, double alt_m)
{
    memset(loc, 0, sizeof(*loc));
    /* SAPIENT: x = longitude, y = latitude, z = altitude */
    loc->has_x = true;
    loc->x = lon_deg;
    loc->has_y = true;
    loc->y = lat_deg;
    loc->has_z = true;
    loc->z = alt_m;
    loc->has_coordinate_system = true;
    loc->coordinate_system = P(LocationCoordinateSystem_LOCATION_COORDINATE_SYSTEM_LAT_LNG_DEG_M);
    loc->has_datum = true;
    loc->datum = P(LocationDatum_LOCATION_DATUM_WGS84_G);
}

void sapient_location_set_error(sapient_location_t *loc, double lat_err_deg, double lon_err_deg, double alt_err_m)
{
    loc->has_x_error = true;
    loc->x_error = lon_err_deg;
    loc->has_y_error = true;
    loc->y_error = lat_err_deg;
    loc->has_z_error = true;
    loc->z_error = alt_err_m;
}

void sapient_range_bearing_deg_m(sapient_range_bearing_t *rb, double azimuth_deg, double elevation_deg,
        double range_m)
{
    memset(rb, 0, sizeof(*rb));
    rb->has_azimuth = true;
    rb->azimuth = azimuth_deg;
    rb->has_elevation = true;
    rb->elevation = elevation_deg;
    if (range_m > 0) {
        rb->has_range = true;
        rb->range = range_m;
    }
    rb->has_coordinate_system = true;
    rb->coordinate_system = P(RangeBearingCoordinateSystem_RANGE_BEARING_COORDINATE_SYSTEM_DEGREES_M);
    rb->has_datum = true;
    rb->datum = P(RangeBearingDatum_RANGE_BEARING_DATUM_TRUE);
}

/* --------------------------------------------------------------- detection */

void sapient_detection_set_location(sapient_detection_report_t *dr, const sapient_location_t *loc)
{
    dr->which_location_oneof = P(DetectionReport_location_tag);
    dr->location_oneof.location = *loc;
}

void sapient_detection_set_range_bearing(sapient_detection_report_t *dr, const sapient_range_bearing_t *rb)
{
    dr->which_location_oneof = P(DetectionReport_range_bearing_tag);
    dr->location_oneof.range_bearing = *rb;
}

bool sapient_detection_add_class(sapient_detection_report_t *dr, const char *type, float confidence)
{
    const size_t max = sizeof(dr->classification) / sizeof(dr->classification[0]);
    if (dr->classification_count >= max) {
        return false;
    }
    P(DetectionReport_DetectionReportClassification) *c = &dr->classification[dr->classification_count++];
    memset(c, 0, sizeof(*c));
    c->has_type = true;
    SAPIENT_STRCPY(c->type, type);
    if (confidence >= 0.0f) {
        c->has_confidence = true;
        c->confidence = confidence;
    }
    return true;
}

bool sapient_detection_add_track_info(sapient_detection_report_t *dr, const char *type, const char *value)
{
    const size_t max = sizeof(dr->track_info) / sizeof(dr->track_info[0]);
    if (dr->track_info_count >= max) {
        return false;
    }
    P(DetectionReport_TrackObjectInfo) *t = &dr->track_info[dr->track_info_count++];
    memset(t, 0, sizeof(*t));
    t->has_type = true;
    SAPIENT_STRCPY(t->type, type);
    t->has_value = true;
    SAPIENT_STRCPY(t->value, value);
    return true;
}

void sapient_detection_set_enu_velocity(sapient_detection_report_t *dr, double east_ms, double north_ms,
        double up_ms)
{
    dr->which_velocity_oneof = P(DetectionReport_enu_velocity_tag);
    P(ENUVelocity) *v = &dr->velocity_oneof.enu_velocity;
    memset(v, 0, sizeof(*v));
    v->has_east_rate = true;
    v->east_rate = east_ms;
    v->has_north_rate = true;
    v->north_rate = north_ms;
    v->has_up_rate = true;
    v->up_rate = up_ms;
}

/* ------------------------------------------------------------------ status */

void sapient_status_set_location(sapient_status_report_t *sr, const sapient_location_t *loc)
{
    sr->has_node_location = true;
    sr->node_location = *loc;
}

bool sapient_status_add(sapient_status_report_t *sr, P(StatusReport_StatusLevel) level,
        P(StatusReport_StatusType) type, const char *value)
{
    const size_t max = sizeof(sr->status) / sizeof(sr->status[0]);
    if (sr->status_count >= max) {
        return false;
    }
    P(StatusReport_Status) *s = &sr->status[sr->status_count++];
    memset(s, 0, sizeof(*s));
    s->has_status_level = true;
    s->status_level = level;
    s->has_status_type = true;
    s->status_type = type;
    if (value) {
        s->has_status_value = true;
        SAPIENT_STRCPY(s->status_value, value);
    }
    return true;
}

void sapient_status_set_power(sapient_status_report_t *sr, P(StatusReport_PowerSource) source,
        P(StatusReport_PowerStatus) status, int32_t level_percent)
{
    sr->has_power = true;
    memset(&sr->power, 0, sizeof(sr->power));
    sr->power.source = source;
    sr->power.status = status;
    if (level_percent >= 0) {
        sr->power.has_level = true;
        sr->power.level = level_percent;
    }
}
