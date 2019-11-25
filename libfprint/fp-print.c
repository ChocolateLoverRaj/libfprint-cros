/*
 * FPrint Print handling
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "fpi-print.h"
#include "fpi-image.h"
#include "fpi-device.h"

#include "nbis/include/bozorth.h"
#include "nbis/include/lfs.h"

/**
 * SECTION: fp-print
 * @title: FpPrint
 * @short_description: Fingerprint handling
 *
 * Interaction with prints and their storage.
 */

/**
 * SECTION: fpi-print
 * @title: Internal FpPrint
 * @short_description: Internal fingerprint handling routines
 *
 * Interaction with prints and their storage. See also the public
 * #FpPrint routines.
 */

struct _FpPrint
{
  GInitiallyUnowned parent_instance;

  FpPrintType       type;

  gchar            *driver;
  gchar            *device_id;
  gboolean          device_stored;

  FpImage          *image;

  /* Metadata */
  FpFinger   finger;
  gchar     *username;
  gchar     *description;
  GDate     *enroll_date;

  GVariant  *data;
  GPtrArray *prints;
};

G_DEFINE_TYPE (FpPrint, fp_print, G_TYPE_INITIALLY_UNOWNED)

enum {
  PROP_0,
  PROP_DRIVER,
  PROP_DEVICE_ID,
  PROP_DEVICE_STORED,
  PROP_IMAGE,

  /* The following is metadata that is stored by default for each print.
   * Drivers may make use of these during enrollment (e.g. to additionaly store
   * the metadata on the device). */
  PROP_FINGER,
  PROP_USERNAME,
  PROP_DESCRIPTION,
  PROP_ENROLL_DATE,

  /* Private property*/
  PROP_FPI_TYPE,
  PROP_FPI_DATA,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
fp_print_finalize (GObject *object)
{
  FpPrint *self = (FpPrint *) object;

  g_clear_object (&self->image);
  g_clear_pointer (&self->device_id, g_free);
  g_clear_pointer (&self->driver, g_free);
  g_clear_pointer (&self->username, g_free);
  g_clear_pointer (&self->description, g_free);
  g_clear_pointer (&self->enroll_date, g_date_free);
  g_clear_pointer (&self->data, g_variant_unref);

  G_OBJECT_CLASS (fp_print_parent_class)->finalize (object);
}

static void
fp_print_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  FpPrint *self = FP_PRINT (object);

  switch (prop_id)
    {
    case PROP_DRIVER:
      g_value_set_string (value, self->driver);
      break;

    case PROP_DEVICE_ID:
      g_value_set_string (value, self->device_id);
      break;

    case PROP_DEVICE_STORED:
      g_value_set_boolean (value, self->device_stored);
      break;

    case PROP_IMAGE:
      g_value_set_object (value, self->image);
      break;

    case PROP_FINGER:
      g_value_set_enum (value, self->finger);
      break;

    case PROP_USERNAME:
      g_value_set_string (value, self->username);
      break;

    case PROP_DESCRIPTION:
      g_value_set_string (value, self->description);
      break;

    case PROP_ENROLL_DATE:
      g_value_set_boxed (value, self->enroll_date);
      break;

    case PROP_FPI_TYPE:
      g_value_set_enum (value, self->type);
      break;

    case PROP_FPI_DATA:
      g_value_set_variant (value, self->data);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
fp_print_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  FpPrint *self = FP_PRINT (object);

  switch (prop_id)
    {
    case PROP_FPI_TYPE:
      fpi_print_set_type (self, g_value_get_enum (value));
      break;

    case PROP_DRIVER:
      self->driver = g_value_dup_string (value);
      break;

    case PROP_DEVICE_ID:
      self->device_id = g_value_dup_string (value);
      break;

    case PROP_DEVICE_STORED:
      self->device_stored = g_value_get_boolean (value);
      break;

    case PROP_FINGER:
      self->finger = g_value_get_enum (value);
      break;

    case PROP_USERNAME:
      g_clear_pointer (&self->username, g_free);
      self->username = g_value_dup_string (value);
      break;

    case PROP_DESCRIPTION:
      g_clear_pointer (&self->description, g_free);
      self->description = g_value_dup_string (value);
      break;

    case PROP_ENROLL_DATE:
      g_clear_pointer (&self->enroll_date, g_date_free);
      self->enroll_date = g_value_dup_boxed (value);
      break;

    case PROP_FPI_DATA:
      g_clear_pointer (&self->description, g_variant_unref);
      self->data = g_value_dup_variant (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
fp_print_constructed (GObject *obj)
{
  FpPrint *self = FP_PRINT (obj);

  g_assert (self->driver != NULL);
  g_assert (self->device_id != NULL);
}

static void
fp_print_class_init (FpPrintClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = fp_print_constructed;
  object_class->finalize = fp_print_finalize;
  object_class->get_property = fp_print_get_property;
  object_class->set_property = fp_print_set_property;

  properties[PROP_DRIVER] =
    g_param_spec_string ("driver",
                         "Driver",
                         "The name of the driver that created the print",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_DEVICE_ID] =
    g_param_spec_string ("device-id",
                         "Device ID",
                         "Unique ID allowing to check if a device is compatible with the print",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_DEVICE_STORED] =
    g_param_spec_boolean ("device-stored",
                          "Device Stored",
                          "Whether the print is a handle for data that is stored on the device",
                          FALSE,
                          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_IMAGE] =
    g_param_spec_object ("image",
                         "Image",
                         "The image that was used for the print, only valid for newly enrolled prints on image based devices",
                         FP_TYPE_IMAGE,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READABLE);

  properties[PROP_FINGER] =
    g_param_spec_enum ("finger",
                       "Finger",
                       "The enrolled finger",
                       FP_TYPE_FINGER,
                       FP_FINGER_UNKNOWN,
                       G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  properties[PROP_USERNAME] =
    g_param_spec_string ("username",
                         "Username",
                         "The username that the enrolled print belongs to",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  properties[PROP_DESCRIPTION] =
    g_param_spec_string ("description",
                         "Description",
                         "A user defined description for the print",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  properties[PROP_ENROLL_DATE] =
    g_param_spec_boxed ("enroll-date",
                        "Enroll Date",
                        "The date of enrollment",
                        G_TYPE_DATE,
                        G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  properties[PROP_FPI_TYPE] =
    g_param_spec_enum ("fp-type",
                       "Type",
                       "Private: The type of the print data",
                       FP_TYPE_PRINT_TYPE,
                       FP_PRINT_RAW,
                       G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_FPI_DATA] =
    g_param_spec_variant ("fp-data",
                          "Raw Data",
                          "The raw data for internal use only",
                          G_VARIANT_TYPE_ANY,
                          NULL,
                          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
fp_print_init (FpPrint *self)
{
}

/**
 * fp_print_new:
 * @device: A #FpDevice
 *
 * Create a new #FpPrint. This is only useful to prepare an enrollment
 * of a new print using fp_device_enroll(). For this you should first
 * create a new print, fill in the relevant metadata, and then start
 * enrollment.
 *
 * Returns: (transfer floating): A newyl created #FpPrint
 */
FpPrint *
fp_print_new (FpDevice *device)
{
  g_return_val_if_fail (device, NULL);

  return g_object_new (FP_TYPE_PRINT,
                       "driver", fp_device_get_driver (device),
                       "device-id", fp_device_get_device_id (device),
                       NULL);
}

/**
 * fp_print_get_driver:
 * @print: A #FpPrint
 *
 * Returns the driver that the print was created for.
 *
 * Returns: (transfer none): The driver
 */
const gchar *
fp_print_get_driver (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), NULL);

  return print->driver;
}

/**
 * fp_print_get_device_id:
 * @print: A #FpPrint
 *
 * Returns the device ID that the print was created for.
 *
 * Returns: (transfer none): The device ID
 */
const gchar *
fp_print_get_device_id (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), NULL);

  return print->device_id;
}

/**
 * fp_print_get_device_stored:
 * @print: A #FpPrint
 *
 * Whether the print is actually stored on the device and this is
 * just a handle to use that references the device stored data.
 *
 * Returns: Whether the print is stored on the device
 */
gboolean
fp_print_get_device_stored (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), FALSE);

  return print->device_stored;
}

/**
 * fp_print_get_image:
 * @print: A #FpPrint
 *
 * Returns the image that the print was created from, or %NULL
 *
 * Returns: (transfer none) (nullable): The #FpImage
 */
FpImage *
fp_print_get_image (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), NULL);

  return print->image;
}

/**
 * fp_print_get_finger:
 * @print: A #FpPrint
 *
 * Returns the finger that the print was created for.
 *
 * Returns: The #FpFinger
 */
FpFinger
fp_print_get_finger (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), FP_FINGER_UNKNOWN);

  return print->finger;
}

/**
 * fp_print_get_username:
 * @print: A #FpPrint
 *
 * Returns the user defined username for the print.
 *
 * Returns: (transfer none) (nullable): The username
 */
const gchar *
fp_print_get_username (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), NULL);

  return print->username;
}

/**
 * fp_print_get_description:
 * @print: A #FpPrint
 *
 * Returns the user defined description for the print.
 *
 * Returns: (transfer none) (nullable): The description
 */
const gchar *
fp_print_get_description (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), NULL);

  return print->description;
}

/**
 * fp_print_get_enroll_date:
 * @print: A #FpPrint
 *
 * Returns the user defined enroll date for the print.
 *
 * Returns: (transfer none) (nullable): The #GDate
 */
const GDate *
fp_print_get_enroll_date (FpPrint *print)
{
  g_return_val_if_fail (FP_IS_PRINT (print), NULL);

  return print->enroll_date;
}

/**
 * fp_print_set_finger:
 * @print: A #FpPrint
 * @finger: The #FpFinger
 *
 * Set the finger that the print is for.
 */
void
fp_print_set_finger (FpPrint *print,
                     FpFinger finger)
{
  g_return_if_fail (FP_IS_PRINT (print));

  print->finger = finger;
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_FINGER]);
}

/**
 * fp_print_set_username:
 * @print: A #FpPrint
 * @username: (transfer none): The new username
 *
 * Set the username for the print.
 */
void
fp_print_set_username (FpPrint     *print,
                       const gchar *username)
{
  g_return_if_fail (FP_IS_PRINT (print));

  g_clear_pointer (&print->username, g_free);
  print->username = g_strdup (username);
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_USERNAME]);
}

/**
 * fp_print_set_description:
 * @print: A #FpPrint
 * @description: (transfer none): The new description
 *
 * Set the description for the print.
 */
void
fp_print_set_description (FpPrint     *print,
                          const gchar *description)
{
  g_return_if_fail (FP_IS_PRINT (print));

  g_clear_pointer (&print->description, g_free);
  print->description = g_strdup (description);
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_DESCRIPTION]);
}

/**
 * fp_print_set_enroll_date:
 * @print: A #FpPrint
 * @enroll_date: (transfer none): The new enroll date
 *
 * Set the enroll date for the print.
 */
void
fp_print_set_enroll_date (FpPrint     *print,
                          const GDate *enroll_date)
{
  g_return_if_fail (FP_IS_PRINT (print));

  g_clear_pointer (&print->enroll_date, g_date_free);
  if (enroll_date)
    {
      /* XXX: Should use g_date_copy, but that is new in 2.56. */
      print->enroll_date = g_date_new ();
      *print->enroll_date = *enroll_date;
    }
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_ENROLL_DATE]);
}



/**
 * fpi_print_add_print:
 * @print: A #FpPrint
 * @add: Print to append to @print
 *
 * Appends the single #FP_PRINT_NBIS print from @add to the collection of
 * prints in @print. Both print objects need to be of type #FP_PRINT_NBIS
 * for this to work.
 */
void
fpi_print_add_print (FpPrint *print, FpPrint *add)
{
  g_return_if_fail (print->type == FP_PRINT_NBIS);
  g_return_if_fail (add->type == FP_PRINT_NBIS);

  g_assert (add->prints->len == 1);
  g_ptr_array_add (print->prints, g_memdup (add->prints->pdata[0], sizeof (struct xyt_struct)));
}

/**
 * fpi_print_set_type:
 * @print: A #FpPrint
 * @type: The newly type of the print data
 *
 * This function can only be called exactly once. Drivers should
 * call it after creating a new print, or to initialize the template
 * print passed during enrollment.
 */
void
fpi_print_set_type (FpPrint    *print,
                    FpPrintType type)
{
  g_return_if_fail (FP_IS_PRINT (print));
  /* We only allow setting this once! */
  g_return_if_fail (print->type == FP_PRINT_UNDEFINED);

  print->type = type;
  if (print->type == FP_PRINT_NBIS)
    print->prints = g_ptr_array_new_with_free_func (g_free);
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_FPI_TYPE]);
}

/**
 * fpi_print_set_device_stored:
 * @print: A #FpPrint
 * @device_stored: Whether the print is stored on the device or not
 *
 * Drivers must set this to %TRUE for any print that is really a handle
 * for data that is stored on the device itself.
 */
void
fpi_print_set_device_stored (FpPrint *print,
                             gboolean device_stored)
{
  g_return_if_fail (FP_IS_PRINT (print));

  print->device_stored = device_stored;
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_DEVICE_STORED]);
}

/* XXX: This is the old version, but wouldn't it be smarter to instead
 * use the highest quality mintutiae? Possibly just using bz_prune from
 * upstream? */
static void
minutiae_to_xyt (struct fp_minutiae *minutiae,
                 int                 bwidth,
                 int                 bheight,
                 struct xyt_struct  *xyt)
{
  int i;
  struct fp_minutia *minutia;
  struct minutiae_struct c[MAX_FILE_MINUTIAE];

  /* struct xyt_struct uses arrays of MAX_BOZORTH_MINUTIAE (200) */
  int nmin = min (minutiae->num, MAX_BOZORTH_MINUTIAE);

  for (i = 0; i < nmin; i++)
    {
      minutia = minutiae->list[i];

      lfs2nist_minutia_XYT (&c[i].col[0], &c[i].col[1], &c[i].col[2],
                            minutia, bwidth, bheight);
      c[i].col[3] = sround (minutia->reliability * 100.0);

      if (c[i].col[2] > 180)
        c[i].col[2] -= 360;
    }

  qsort ((void *) &c, (size_t) nmin, sizeof (struct minutiae_struct),
         sort_x_y);

  for (i = 0; i < nmin; i++)
    {
      xyt->xcol[i]     = c[i].col[0];
      xyt->ycol[i]     = c[i].col[1];
      xyt->thetacol[i] = c[i].col[2];
    }
  xyt->nrows = nmin;
}

/**
 * fpi_print_add_from_image:
 * @print: A #FpPrint
 * @image: A #FpImage
 * @error: Return location for error
 *
 * Extracts the minutiae from the given image and adds it to @print of
 * type #FP_PRINT_NBIS.
 *
 * The @image will be kept so that API users can get retrieve it e.g.
 * for debugging purposes.
 *
 * Returns: %TRUE on success
 */
gboolean
fpi_print_add_from_image (FpPrint *print,
                          FpImage *image,
                          GError **error)
{
  GPtrArray *minutiae;
  struct fp_minutiae _minutiae;
  struct xyt_struct *xyt;

  if (print->type != FP_PRINT_NBIS || !image)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Cannot add print data from image!");
      return FALSE;
    }

  minutiae = fp_image_get_minutiae (image);
  if (!minutiae || minutiae->len == 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "No minutiae found in image or not yet detected!");
      return FALSE;
    }

  _minutiae.num = minutiae->len;
  _minutiae.list = (struct fp_minutia **) minutiae->pdata;
  _minutiae.alloc = minutiae->len;

  xyt = g_new0 (struct xyt_struct, 1);
  minutiae_to_xyt (&_minutiae, image->width, image->height, xyt);
  g_ptr_array_add (print->prints, xyt);

  g_clear_object (&print->image);
  print->image = g_object_ref (image);
  g_object_notify_by_pspec (G_OBJECT (print), properties[PROP_IMAGE]);

  return TRUE;
}

/**
 * fpi_print_bz3_match:
 * @template: A #FpPrint containing one or more prints
 * @print: A newly scanned #FpPrint to test
 * @bz3_threshold: The BZ3 match threshold
 * @error: Return location for error
 *
 * Match the newly scanned @print (containing exactly one print) against the
 * prints contained in @template which will have been stored during enrollment.
 *
 * Both @template and @print need to be of type #FP_PRINT_NBIS for this to
 * work.
 *
 * Returns: Whether the prints match, @error will be set if #FPI_MATCH_ERROR is returned
 */
FpiMatchResult
fpi_print_bz3_match (FpPrint *template, FpPrint *print, gint bz3_threshold, GError **error)
{
  struct xyt_struct *pstruct;
  gint probe_len;
  gint i;

  /* XXX: Use a different error type? */
  if (template->type != FP_PRINT_NBIS || print->type != FP_PRINT_NBIS)
    {
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_NOT_SUPPORTED,
                                         "It is only possible to match NBIS type print data");
      return FPI_MATCH_ERROR;
    }

  if (print->prints->len != 1)
    {
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                         "New print contains more than one print!");
      return FPI_MATCH_ERROR;
    }

  pstruct = g_ptr_array_index (print->prints, 0);
  probe_len = bozorth_probe_init (pstruct);

  for (i = 0; i < template->prints->len; i++)
    {
      struct xyt_struct *gstruct;
      gint score;
      gstruct = g_ptr_array_index (template->prints, i);
      score = bozorth_to_gallery (probe_len, pstruct, gstruct);
      fp_dbg ("score %d", score);

      if (score >= bz3_threshold)
        return FPI_MATCH_SUCCESS;
    }

  return FPI_MATCH_FAIL;
}

/**
 * fp_print_compatible:
 * @self: A #FpPrint
 * @device: A #FpDevice
 *
 * Tests whether the prints is compatible with the given device.
 *
 * Returns: %TRUE if the print is compatible with the device
 */
gboolean
fp_print_compatible (FpPrint *self, FpDevice *device)
{
  g_return_val_if_fail (FP_IS_PRINT (self), FALSE);
  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  if (g_strcmp0 (self->driver, fp_device_get_driver (device)))
    return FALSE;

  if (g_strcmp0 (self->device_id, fp_device_get_device_id (device)))
    return FALSE;

  return TRUE;
}

/**
 * fp_print_equal:
 * @self: First #FpPrint
 * @other: Second #FpPrint
 *
 * Tests whether the prints can be considered equal. This only compares the
 * actual information about the print, not the metadata.
 *
 * Returns: %TRUE if the prints are equal
 */
gboolean
fp_print_equal (FpPrint *self, FpPrint *other)
{
  g_return_val_if_fail (FP_IS_PRINT (self), FALSE);
  g_return_val_if_fail (FP_IS_PRINT (other), FALSE);
  g_return_val_if_fail (self->type != FP_PRINT_UNDEFINED, FALSE);
  g_return_val_if_fail (other->type != FP_PRINT_UNDEFINED, FALSE);

  if (self->type != other->type)
    return FALSE;

  if (g_strcmp0 (self->driver, other->driver))
    return FALSE;

  if (g_strcmp0 (self->device_id, other->device_id))
    return FALSE;

  if (self->type == FP_PRINT_RAW)
    {
      return g_variant_equal (self->data, other->data);
    }
  else if (self->type == FP_PRINT_NBIS)
    {
      gint i;

      if (self->prints->len != other->prints->len)
        return FALSE;

      for (i = 0; i < self->prints->len; i++)
        {
          struct xyt_struct *a = g_ptr_array_index (self->prints, i);
          struct xyt_struct *b = g_ptr_array_index (other->prints, i);

          if (memcmp (a, b, sizeof (struct xyt_struct)) != 0)
            return FALSE;
        }

      return TRUE;
    }
  else
    {
      g_assert_not_reached ();
    }
}

#define FP_PRINT_VARIANT_TYPE G_VARIANT_TYPE ("(issbymsmsia{sv}v)")

G_STATIC_ASSERT (sizeof (((struct xyt_struct *) NULL)->xcol[0]) == 4);

/**
 * fp_print_serialize:
 * @print: A #FpPrint
 * @data: (array length=length) (transfer full) (out): Return location for data pointer
 * @length: (transfer full) (out): Length of @data
 * @error: Return location for error
 *
 * Serialize a print definition for permanent storage. Note that this is
 * lossy in the sense that e.g. the image data is discarded.
 *
 * Returns: (type void): %TRUE on success
 */
gboolean
fp_print_serialize (FpPrint *print,
                    guchar **data,
                    gsize   *length,
                    GError **error)
{
  g_autoptr(GVariant) result = NULL;
  GVariantBuilder builder = G_VARIANT_BUILDER_INIT (FP_PRINT_VARIANT_TYPE);
  gsize len;

  g_assert (data);
  g_assert (length);

  g_variant_builder_add (&builder, "i", print->type);
  g_variant_builder_add (&builder, "s", print->driver);
  g_variant_builder_add (&builder, "s", print->device_id);
  g_variant_builder_add (&builder, "b", print->device_stored);

  /* Metadata */
  g_variant_builder_add (&builder, "y", print->finger);
  g_variant_builder_add (&builder, "ms", print->username);
  g_variant_builder_add (&builder, "ms", print->description);
  if (print->enroll_date && g_date_valid (print->enroll_date))
    g_variant_builder_add (&builder, "i", g_date_get_julian (print->enroll_date));
  else
    g_variant_builder_add (&builder, "i", G_MININT32);

  /* Unused a{sv} for expansion */
  g_variant_builder_open (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_close (&builder);

  /* Insert NBIS print data for type NBIS, otherwise the GVariant directly */
  if (print->type == FP_PRINT_NBIS)
    {
      GVariantBuilder nested = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("(a(aiaiai))"));
      gint i;

      g_variant_builder_open (&nested, G_VARIANT_TYPE ("a(aiaiai)"));
      for (i = 0; i < print->prints->len; i++)
        {
          struct xyt_struct *xyt = g_ptr_array_index (print->prints, i);
          gint j;
          gint32 *col = g_new (gint32, xyt->nrows);

          g_variant_builder_open (&nested, G_VARIANT_TYPE ("(aiaiai)"));

          for (j = 0; j < xyt->nrows; j++)
            col[j] = GINT32_TO_LE (xyt->xcol[j]);
          g_variant_builder_add_value (&nested,
                                       g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                                                  col,
                                                                  xyt->nrows,
                                                                  sizeof (col[0])));

          for (j = 0; j < xyt->nrows; j++)
            col[j] = GINT32_TO_LE (xyt->ycol[j]);
          g_variant_builder_add_value (&nested,
                                       g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                                                  col,
                                                                  xyt->nrows,
                                                                  sizeof (col[0])));

          for (j = 0; j < xyt->nrows; j++)
            col[j] = GINT32_TO_LE (xyt->thetacol[j]);
          g_variant_builder_add_value (&nested,
                                       g_variant_new_fixed_array (G_VARIANT_TYPE_INT32,
                                                                  col,
                                                                  xyt->nrows,
                                                                  sizeof (col[0])));
          g_variant_builder_close (&nested);
          g_free (col);
        }

      g_variant_builder_close (&nested);
      g_variant_builder_add (&builder, "v", g_variant_builder_end (&nested));
    }
  else
    {
      g_variant_builder_add (&builder, "v", g_variant_new_variant (print->data));
    }

  result = g_variant_builder_end (&builder);

  if (G_BYTE_ORDER == G_BIG_ENDIAN)
    {
      GVariant *tmp;
      tmp = g_variant_byteswap (result);
      g_variant_unref (result);
      result = tmp;
    }

  len = g_variant_get_size (result);
  /* Add 3 bytes of header */
  len += 3;

  *data = g_malloc (len);
  *length = len;

  (*data)[0] = (guchar) 'F';
  (*data)[1] = (guchar) 'P';
  (*data)[2] = (guchar) '3';

  g_variant_get_data (result);
  g_variant_store (result, (*data) + 3);

  return TRUE;
}

/**
 * fp_print_deserialize:
 * @data: (array length=length): The binary data
 * @length: Length of the data
 * @error: Return location for error
 *
 * Deserialize a print definition from permanent storage.
 *
 * Returns: (transfer full): A newly created #FpPrint on success
 */
FpPrint *
fp_print_deserialize (const guchar *data,
                      gsize         length,
                      GError      **error)
{
  g_autoptr(FpPrint) result = NULL;
  g_autoptr(GVariant) raw_value = NULL;
  g_autoptr(GVariant) value = NULL;
  guchar *aligned_data = NULL;
  GDate *date = NULL;
  guint8 finger_int8;
  FpFinger finger;
  g_autofree gchar *username = NULL;
  g_autofree gchar *description = NULL;
  gint julian_date;
  FpPrintType type;
  const gchar *driver;
  const gchar *device_id;
  gboolean device_stored;
  GVariant *print_data;

  g_assert (data);
  g_assert (length > 3);

  if (memcmp (data, "FP3", 3) != 0)
    goto invalid_format;

  /* NOTE:
   * We make sure that we have no variant left over from the parsing at the end
   * of this function (meaning we don't need to keep the data around.
   */

  /* To support GLIB < 2.60 we need to make sure that the memory is aligned correctly.
   * We also need to copy the backing store for the raw data that we may keep for
   * longer. */
  aligned_data = g_malloc (length - 3);
  memcpy (aligned_data, data + 3, length - 3);
  raw_value = g_variant_new_from_data (FP_PRINT_VARIANT_TYPE,
                                       aligned_data, length - 3,
                                       FALSE, g_free, NULL);

  if (!raw_value)
    goto invalid_format;

  if (G_BYTE_ORDER == G_BIG_ENDIAN)
    value = g_variant_byteswap (raw_value);
  else
    value = g_variant_get_normal_form (raw_value);

  g_variant_get (value,
                 "(issbymsmsi@a{sv}v)",
                 &type,
                 &driver,
                 &device_id,
                 &device_stored,
                 &finger_int8,
                 &username,
                 &description,
                 &julian_date,
                 NULL,
                 &print_data);

  finger = finger_int8;

  /* Assume data is valid at this point if the values are somewhat sane. */
  if (type == FP_PRINT_NBIS)
    {
      g_autoptr(GVariant) prints = g_variant_get_child_value (print_data, 0);
      gint i;

      result = g_object_new (FP_TYPE_PRINT,
                             "driver", driver,
                             "device-id", device_id,
                             "device-stored", device_stored,
                             NULL);
      fpi_print_set_type (result, FP_PRINT_NBIS);
      for (i = 0; i < g_variant_n_children (prints); i++)
        {
          struct xyt_struct *xyt = g_new0 (struct xyt_struct, 1);
          const gint32 *xcol, *ycol, *thetacol;
          gsize xlen, ylen, thetalen;
          g_autoptr(GVariant) xyt_data = NULL;
          GVariant *child;

          xyt_data = g_variant_get_child_value (prints, i);

          child = g_variant_get_child_value (xyt_data, 0);
          xcol = g_variant_get_fixed_array (child, &xlen, sizeof (gint32));
          g_variant_unref (child);

          child = g_variant_get_child_value (xyt_data, 1);
          ycol = g_variant_get_fixed_array (child, &ylen, sizeof (gint32));
          g_variant_unref (child);

          child = g_variant_get_child_value (xyt_data, 2);
          thetacol = g_variant_get_fixed_array (child, &thetalen, sizeof (gint32));
          g_variant_unref (child);

          if (xlen != ylen || xlen != thetalen)
            goto invalid_format;

          if (xlen > G_N_ELEMENTS (xyt->xcol))
            goto invalid_format;

          xyt->nrows = xlen;
          memcpy (xyt->xcol, xcol, sizeof (xcol[0]) * xlen);
          memcpy (xyt->ycol, ycol, sizeof (xcol[0]) * xlen);
          memcpy (xyt->thetacol, thetacol, sizeof (xcol[0]) * xlen);

          g_ptr_array_add (result->prints, xyt);
        }
    }
  else if (type == FP_PRINT_RAW)
    {
      g_autoptr(GVariant) fp_data = g_variant_get_child_value (print_data, 0);

      result = g_object_new (FP_TYPE_PRINT,
                             "fp-type", type,
                             "driver", driver,
                             "device-id", device_id,
                             "device-stored", device_stored,
                             "fp-data", fp_data,
                             NULL);
    }
  else
    {
      g_warning ("Invalid print type: 0x%X", type);
      goto invalid_format;
    }

  date = g_date_new_julian (julian_date);
  g_object_set (result,
                "finger", finger,
                "username", username,
                "description", description,
                "enroll_date", date,
                NULL);

  g_date_free (date);

  return g_steal_pointer (&result);

invalid_format:
  *error = g_error_new_literal (G_IO_ERROR,
                                G_IO_ERROR_INVALID_DATA,
                                "Data could not be parsed");
  return FALSE;
}
