/*
 * ngfd - Non-graphic feedback daemon
 *
 * Copyright (C) 2010 Nokia Corporation.
 * Contact: Xun Chen <xun.chen@nokia.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
 
#include <ngf/plugin.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <mce/dbus-names.h>

#define MCE_KEY "plugin.mce.data"
#define LOG_CAT "mce: "
#define DISPLAY_BLANK_TIMEOUT 50

N_PLUGIN_NAME        ("mce")
N_PLUGIN_VERSION     ("0.1")
N_PLUGIN_DESCRIPTION ("MCE plugin for handling backlight and led actions")

typedef struct _MceData
{
    NRequest       *request;
    NSinkInterface *iface;
    gchar          *pattern;
    guint           timeout_id;
} MceData;

DBusConnection *bus = NULL;

static gboolean
call_dbus_method (DBusConnection *bus, DBusMessage *msg)
{
    if (!dbus_connection_send (bus, msg, 0)) {
        N_WARNING ("Failed to send DBus message %s to interface %s", dbus_message_get_member (msg), dbus_message_get_interface (msg));
        return FALSE;
    }

    return TRUE;
}

static gboolean
backlight_on (void)
{
    DBusMessage *msg = NULL;
    gboolean     ret = FALSE;

    if (bus == NULL)
        return FALSE;

    msg = dbus_message_new_method_call (MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF, MCE_DISPLAY_ON_REQ);
    if (msg == NULL)
        return FALSE;

    ret = call_dbus_method (bus, msg);
    dbus_message_unref (msg);

    return ret;
}

static gboolean
toggle_pattern (const char *pattern, gboolean activate)
{
    DBusMessage *msg = NULL;
    gboolean     ret = FALSE;

    if (bus == NULL || pattern == NULL)
        return FALSE;

    msg = dbus_message_new_method_call (MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF,
        activate ? MCE_ACTIVATE_LED_PATTERN : MCE_DEACTIVATE_LED_PATTERN);

    if (msg == NULL)
        return FALSE;

    if (!dbus_message_append_args (msg, DBUS_TYPE_STRING, &pattern, DBUS_TYPE_INVALID)) {
        dbus_message_unref (msg);
        return FALSE;
    }

    ret = call_dbus_method (bus, msg);
    dbus_message_unref (msg);

    if (ret)
        N_DEBUG ("%s >> led pattern %s %s.", __FUNCTION__, pattern, activate ? "activated" : "deactivated");

    return ret;
}

static int
mce_sink_initialize (NSinkInterface *iface)
{
    (void) iface;
    DBusError       error;

    dbus_error_init (&error);
    bus = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
    if (bus == NULL) {
        N_WARNING ("%s >> failed to get system bus: %s",error.message);
        dbus_error_free (&error);
        return FALSE;
    }
    
    return TRUE;
}

static void
mce_sink_shutdown (NSinkInterface *iface)
{
    (void) iface;
}

static int
mce_sink_can_handle (NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    const NProplist *props = n_request_get_properties (request);

    if (n_proplist_has_key (props, "mce.backlight_on") || n_proplist_has_key (props, "mce.led_pattern")) {
        return TRUE;
    }

    return FALSE;
}

static int
mce_sink_prepare (NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;
    
    MceData *data = g_slice_new0 (MceData);

    data->request    = request;
    data->iface      = iface;

    n_request_store_data (request, MCE_KEY, data);
    n_sink_interface_synchronize (iface, request);
    
    return TRUE;
}

static gboolean
mce_playback_done(gpointer userdata)
{
    MceData *data = (MceData *) userdata;

    data->timeout_id = 0;
    n_sink_interface_complete(data->iface, data->request);

    return FALSE;
}

static int
mce_sink_play (NSinkInterface *iface, NRequest *request)
{
    const NProplist *props = n_request_get_properties (request);
    (void) iface;
    const gchar *pattern = NULL;

    MceData *data = (MceData*) n_request_get_data (request, MCE_KEY);
    g_assert (data != NULL);

    if (n_proplist_get_bool (props, "mce.backlight_on"))
        backlight_on ();

    pattern = n_proplist_get_string (props, "mce.led_pattern");
    if (pattern != NULL) {
        data->pattern = g_strdup (pattern);
        if (!toggle_pattern (pattern, TRUE)) {
            g_free (data->pattern);
            data->pattern = NULL;
        }
    }

    /* Call n_sink_interface_complete() after 100ms. */
    data->timeout_id = g_timeout_add(100, mce_playback_done, data);

    return TRUE;
}

static int
mce_sink_pause (NSinkInterface *iface, NRequest *request)
{
    (void) iface;
    (void) request;

    return TRUE;
}

static void
mce_sink_stop (NSinkInterface *iface, NRequest *request)
{
    (void) iface;

    MceData *data = (MceData*) n_request_get_data (request, MCE_KEY);
    g_assert (data != NULL);

    if (data->pattern) {
        g_free (data->pattern);
        data->pattern = NULL;
    }

    g_slice_free (MceData, data);
}

N_PLUGIN_LOAD (plugin)
{
    static const NSinkInterfaceDecl decl = {
        .name       = "mce",
        .initialize = mce_sink_initialize,
        .shutdown   = mce_sink_shutdown,
        .can_handle = mce_sink_can_handle,
        .prepare    = mce_sink_prepare,
        .play       = mce_sink_play,
        .pause      = mce_sink_pause,
        .stop       = mce_sink_stop
    };

    n_plugin_register_sink (plugin, &decl);

    return TRUE;
}

N_PLUGIN_UNLOAD (plugin)
{
    (void) plugin;
}
