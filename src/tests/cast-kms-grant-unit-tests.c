/*
 * Copyright (C) 2026 Red Hat
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gio/gunixfdlist.h>
#include <glib.h>

#include "backends/native/meta-cast-kms-grant-policy.h"
#include "backends/native/meta-kms-cast-uapi.h"

#include "meta-dbus-cast-kms.h"

static char *
build_argument_signature (GDBusArgInfo **arguments)
{
  g_autoptr (GString) signature = g_string_new (NULL);
  int i;

  for (i = 0; arguments[i]; i++)
    g_string_append (signature, arguments[i]->signature);

  return g_string_free (g_steal_pointer (&signature), FALSE);
}

static void
test_grant_profiles (void)
{
  uint32_t rights;

  g_assert_true (meta_cast_kms_grant_profile_get_rights (
                   META_CAST_KMS_GRANT_PROFILE_DISPLAY_V1,
                   &rights));
  g_assert_cmphex (rights,
                   ==,
                   DRM_CASTKMS_GRANT_CAPTURE_PIXELS |
                   DRM_CASTKMS_GRANT_MANAGE_ATTACHMENT |
                   DRM_CASTKMS_GRANT_UPDATE_EDID |
                   DRM_CASTKMS_GRANT_READ_CURSOR);

  g_assert_true (meta_cast_kms_grant_profile_get_rights (
                   META_CAST_KMS_GRANT_PROFILE_DISPLAY_CEC_V1,
                   &rights));
  g_assert_cmphex (rights,
                   ==,
                   DRM_CASTKMS_GRANT_CAPTURE_PIXELS |
                   DRM_CASTKMS_GRANT_MANAGE_ATTACHMENT |
                   DRM_CASTKMS_GRANT_UPDATE_EDID |
                   DRM_CASTKMS_GRANT_READ_CURSOR |
                   DRM_CASTKMS_GRANT_MANAGE_CEC);

  rights = 0xfeedface;
  g_assert_false (meta_cast_kms_grant_profile_get_rights (0, &rights));
  g_assert_cmphex (rights, ==, 0xfeedface);
  g_assert_false (meta_cast_kms_grant_profile_get_rights (3, &rights));
  g_assert_cmphex (rights, ==, 0xfeedface);
  g_assert_false (meta_cast_kms_grant_profile_get_rights (UINT16_MAX,
                                                          &rights));
  g_assert_cmphex (rights, ==, 0xfeedface);
}

static void
test_control_fd_uapi_contract (void)
{
  g_assert_cmpuint (DRM_CASTKMS_CAPTURE_UAPI_MAJOR, ==, 0);
  g_assert_cmpuint (DRM_CASTKMS_CAPTURE_UAPI_MINOR, ==, 10);
  g_assert_cmphex (DRM_CASTKMS_CAPTURE_CAP_GRANT_CONTROL_FD,
                   ==,
                   1ULL << 4);

  g_assert_cmpuint (sizeof (struct drm_castkms_capture_query_caps), ==, 40);
  g_assert_cmpuint (sizeof (struct drm_castkms_create_grant), ==, 32);
  g_assert_cmpuint (sizeof (struct drm_castkms_get_grant), ==, 32);
  g_assert_cmpuint (
    G_STRUCT_OFFSET (struct drm_castkms_get_grant, output_index),
    ==,
    20);
  g_assert_cmpuint (
    G_STRUCT_OFFSET (struct drm_castkms_create_grant, connector_id),
    ==,
    0);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_castkms_create_grant, rights),
                    ==,
                    4);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_castkms_create_grant, flags),
                    ==,
                    8);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_castkms_create_grant, fd),
                    ==,
                    12);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_castkms_create_grant, grant_id),
                    ==,
                    16);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_castkms_create_grant, fd_flags),
                    ==,
                    20);
  g_assert_cmpuint (
    G_STRUCT_OFFSET (struct drm_castkms_create_grant, control_fd),
    ==,
    24);
  g_assert_cmpuint (G_STRUCT_OFFSET (struct drm_castkms_create_grant, reserved),
                    ==,
                    28);
  g_assert_cmpuint (_IOC_SIZE (DRM_IOCTL_CASTKMS_CREATE_GRANT), ==, 32);
  g_assert_cmpuint (_IOC_NR (DRM_IOCTL_CASTKMS_CREATE_GRANT),
                    ==,
                    DRM_COMMAND_BASE + DRM_CASTKMS_CREATE_GRANT);
}

static void
test_dbus_contract (void)
{
  GDBusInterfaceInfo *interface_info;
  GDBusMethodInfo *method_info;
  g_autofree char *in_signature = NULL;
  g_autofree char *out_signature = NULL;
  GSignalQuery signal_query = {};
  gpointer interface;
  guint signal_id;

  interface_info = meta_dbus_cast_kms_interface_info ();
  g_assert_cmpstr (interface_info->name, ==, "org.gnome.Mutter.CastKms");
  g_assert_nonnull (interface_info->methods);
  g_assert_nonnull (interface_info->methods[0]);
  g_assert_null (interface_info->methods[1]);
  g_assert_null (interface_info->signals);
  g_assert_null (interface_info->properties);

  method_info = interface_info->methods[0];
  g_assert_cmpstr (method_info->name, ==, "CreateCaptureGrant");
  in_signature = build_argument_signature (method_info->in_args);
  out_signature = build_argument_signature (method_info->out_args);
  g_assert_cmpstr (in_signature, ==, "uuuq");
  g_assert_cmpstr (out_signature, ==, "huuuuuqq");
  g_assert_cmpstr (method_info->out_args[0]->name, ==, "holder_fd");
  g_assert_cmpstr (method_info->out_args[1]->name, ==, "grant_id");
  g_assert_cmpstr (method_info->out_args[2]->name, ==, "output_index");
  g_assert_null (method_info->out_args[8]);

  interface = g_type_default_interface_ref (META_DBUS_TYPE_CAST_KMS);
  signal_id = g_signal_lookup ("handle-create-capture-grant",
                               G_TYPE_FROM_INTERFACE (interface));
  g_assert_cmpuint (signal_id, !=, 0);
  g_signal_query (signal_id, &signal_query);
  g_assert_cmpuint (signal_query.n_params, ==, 6);
  g_assert_cmpuint (signal_query.param_types[0],
                    ==,
                    G_TYPE_DBUS_METHOD_INVOCATION);
  g_assert_cmpuint (signal_query.param_types[1], ==, G_TYPE_UNIX_FD_LIST);
  g_type_default_interface_unref (interface);
}

int
main (int    argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/backends/native/cast-kms-grant/profiles",
                   test_grant_profiles);
  g_test_add_func ("/backends/native/cast-kms-grant/control-fd-uapi-contract",
                   test_control_fd_uapi_contract);
  g_test_add_func ("/backends/native/cast-kms-grant/dbus-contract",
                   test_dbus_contract);

  return g_test_run ();
}
