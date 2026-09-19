/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "config.h"

#include <errno.h>
#include <unistd.h>

#include "backends/native/meta-drm-preparation.h"
#include "backends/native/meta-kms-preparation.h"

static int preparation_fd;
static int preparation_write_fd;
static guint preparation_status;

int __wrap_drmIoctl (int fd, unsigned long command, void *request);

int
__wrap_drmIoctl (int            fd,
                 unsigned long  command,
                 void          *request)
{
  if (command == DRM_IOCTL_MODE_PREPARE_REPLACE)
    return dup (preparation_fd);

  g_assert_cmpuint (command, ==, DRM_IOCTL_PREPARE_QUERY);
  g_assert_cmpint (fd, !=, preparation_fd);
  ((struct drm_prepare_query *) request)->status = preparation_status;
  return 0;
}

static MetaKmsPreparation *
create_preparation (void)
{
  int pipe_fds[2];
  uint32_t crtc_id = 7;

  g_assert_cmpint (pipe (pipe_fds), ==, 0);
  preparation_fd = pipe_fds[0];
  preparation_write_fd = pipe_fds[1];
  return meta_kms_preparation_create (preparation_fd, &crtc_id, 1, NULL);
}

static void
destroy_preparation (MetaKmsPreparation *preparation)
{
  meta_kms_preparation_free (preparation);
  close (preparation_fd);
  if (preparation_write_fd >= 0)
    close (preparation_write_fd);
}

static void
test_wait_ready (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsPreparation *preparation = create_preparation ();

  g_assert_nonnull (preparation);
  preparation_status = DRM_PREPARE_READY;
  g_assert_true (meta_kms_preparation_wait_until (preparation,
                                                  g_get_monotonic_time (),
                                                  &error));
  g_assert_no_error (error);
  destroy_preparation (preparation);
}

static void
test_wait_pending_deadline (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsPreparation *preparation = create_preparation ();
  int64_t start_us = g_get_monotonic_time ();

  g_assert_nonnull (preparation);
  preparation_status = DRM_PREPARE_PENDING;
  g_assert_false (meta_kms_preparation_wait_until (preparation,
                                                   start_us + 30 * 1000,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_assert_cmpint (g_get_monotonic_time () - start_us, >=, 30 * 1000);
  destroy_preparation (preparation);
}

static void
test_wait_closed_while_pending (void)
{
  g_autoptr (GError) error = NULL;
  MetaKmsPreparation *preparation = create_preparation ();

  g_assert_nonnull (preparation);
  preparation_status = DRM_PREPARE_PENDING;
  close (preparation_write_fd);
  preparation_write_fd = -1;
  g_assert_false (meta_kms_preparation_wait_until (preparation,
                                                   g_get_monotonic_time () +
                                                     G_USEC_PER_SEC,
                                                   &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_FAILED);
  destroy_preparation (preparation);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/kms/preparation/ready", test_wait_ready);
  g_test_add_func ("/kms/preparation/pending-deadline",
                   test_wait_pending_deadline);
  g_test_add_func ("/kms/preparation/closed-while-pending",
                   test_wait_closed_while_pending);

  return g_test_run ();
}
