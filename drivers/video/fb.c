/****************************************************************************
 * graphics/fb/fb.c
 * Framebuffer character driver
 *
 *   Copyright (C) 2017 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/nx/nx.h>
#include <nuttx/nx/nxglib.h>
#include <nuttx/video/fb.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure defines one framebuffer device.  Note that which is
 * everything in this structure is constant data set up and initialization
 * time.  Therefore, no there is requirement for serialized access to this
 * structure.
 */

struct fb_chardev_s
{
  FAR struct fb_vtable_s *vtable; /* Framebuffer interface */
  FAR void *fbmem;                /* Start of frame buffer memory */
  size_t fblen;                   /* Size of the framebuffer */
  uint8_t plane;                  /* Video plan number */
  uint8_t bpp;                    /* Bits per pixel */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     fb_open(FAR struct file *filep);
static int     fb_close(FAR struct file *filep);
static ssize_t fb_read(FAR struct file *filep, FAR char *buffer,
                 size_t buflen);
static ssize_t fb_write(FAR struct file *filep, FAR const char *buffer,
                 size_t buflen);
static off_t   fb_seek(FAR struct file *filep, off_t offset, int whence);
static int     fb_ioctl(FAR struct file *filep, int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations fb_fops =
{
  fb_open,       /* open */
  fb_close,      /* close */
  fb_read,       /* read */
  fb_write,      /* write */
  fb_seek,       /* seek */
  fb_ioctl       /* ioctl */
#ifndef CONFIG_DISABLE_POLL
  , NULL         /* poll */
#endif
#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
  , NULL         /* unlink */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fb_open
 *
 * Description:
 *   This function is called whenever the framebuffer device is opened.
 *
 ****************************************************************************/

static int fb_open(FAR struct file *filep)
{
  DEBUGASSERT(filep != NULL && filep->f_inode != NULL);
  return OK;
}

/****************************************************************************
 * Name: fb_close
 *
 * Description:
 *   This function is called when the framebuffer device is closed.
 *
 ****************************************************************************/

static int fb_close(FAR struct file *filep)
{
  DEBUGASSERT(filep != NULL && filep->f_inode != NULL);
  return OK;
}

/****************************************************************************
 * Name: fb_read
 ****************************************************************************/

static ssize_t fb_read(FAR struct file *filep, FAR char *buffer, size_t len)
{
  FAR struct inode *inode;
  FAR struct fb_chardev_s *fb;
  size_t start;
  size_t end;
  size_t size;

  ginfo("len: %u\n", (unsigned int)len);

  /* Get the framebuffer instance */

  DEBUGASSERT(filep != NULL && filep->f_inode != NULL);
  inode = filep->f_inode;
  fb    = (FAR struct fb_chardev_s *)inode->i_private;

  /* Get the start and size of the transfer */

  start = filep->f_pos;
  if (start >= fb->fblen)
    {
      return 0;  /* Return end-of-file */
    }

  end = start + len;
  if (end >= fb->fblen)
    {
      end = fb->fblen;
    }

  size = end - start;

  /* And transfer the data from the frame buffer */

  memcpy(buffer, fb->fbmem, size);
  filep->f_pos += size;
  return size;
}

/****************************************************************************
 * Name: fb_write
 ****************************************************************************/

static ssize_t fb_write(FAR struct file *filep, FAR const char *buffer,
                        size_t len)
{
  FAR struct inode *inode;
  FAR struct fb_chardev_s *fb;
  size_t start;
  size_t end;
  size_t size;

  ginfo("len: %u\n", (unsigned int)len);

  /* Get the framebuffer instance */

  DEBUGASSERT(filep != NULL && filep->f_inode != NULL);
  inode = filep->f_inode;
  fb    = (FAR struct fb_chardev_s *)inode->i_private;

  /* Get the start and size of the transfer */

  start = filep->f_pos;
  if (start >= fb->fblen)
    {
      return -EFBIG;  /* Cannot extend the framebuffer */
    }

  end = start + len;
  if (end >= fb->fblen)
    {
      end = fb->fblen;
    }

  size = end - start;

  /* And transfer the data into the frame buffer */

  memcpy(fb->fbmem, buffer, size);
  filep->f_pos += size;
  return size;
}

/****************************************************************************
 * Name: fb_seek
 *
 * Description:
 *   Seek the logical file pointer to the specified position.  The offset
 *   is in units of pixels, with offset zero being the beginning of the
 *   framebuffer.
 *
 ****************************************************************************/

static off_t fb_seek(FAR struct file *filep, off_t offset, int whence)
{
  FAR struct inode *inode;
  FAR struct fb_chardev_s *fb;
  off_t newpos;
  int ret;

  ginfo("offset: %u whence: %d\n", (unsigned int)offset, whence);

  /* Get the framebuffer instance */

  DEBUGASSERT(filep != NULL && filep->f_inode != NULL);
  inode = filep->f_inode;
  fb    = (FAR struct fb_chardev_s *)inode->i_private;

  /* Determine the new, requested file position */

  switch (whence)
    {
    case SEEK_CUR:
      newpos = filep->f_pos + offset;
      break;

    case SEEK_SET:
      newpos = offset;
      break;

    case SEEK_END:
      newpos = fb->fblen + offset;
      break;

    default:
      /* Return EINVAL if the whence argument is invalid */

      return -EINVAL;
    }

  /* Opengroup.org:
   *
   *  "The lseek() function shall allow the file offset to be set beyond the end
   *   of the existing data in the file. If data is later written at this point,
   *   subsequent reads of data in the gap shall return bytes with the value 0
   *   until data is actually written into the gap."
   *
   * We can conform to the first part, but not the second.  But return EINVAL if
   *
   *  "...the resulting file offset would be negative for a regular file, block
   *   special file, or directory."
   */

  if (newpos >= 0)
    {
      filep->f_pos = newpos;
      ret = newpos;
    }
  else
    {
      ret = -EINVAL;
    }

  return ret;
}

/****************************************************************************
 * Name: fb_ioctl
 *
 * Description:
 *   The standard ioctl method.
 *
 ****************************************************************************/

static int fb_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode *inode;
  FAR struct fb_chardev_s *fb;
  int ret;

  ginfo("cmd: %d arg: %ld\n", cmd, arg);

  /* Get the framebuffer instance */

  DEBUGASSERT(filep != NULL && filep->f_inode != NULL);
  inode = filep->f_inode;
  fb    = (FAR struct fb_chardev_s *)inode->i_private;

  /* Process the IOCTL command */

  switch (cmd)
    {
      case FIOC_MMAP:  /* Get color plane info */
        {
          FAR void **ppv = (FAR void **)((uintptr_t)arg);

          /* Return the address corresponding to the start of frame buffer. */

          DEBUGASSERT(ppv != NULL);
          *ppv = fb->fbmem;
          ret = OK;
        }
        break;

      case FBIOGET_VIDEOINFO:  /* Get color plane info */
        {
          FAR struct fb_videoinfo_s *vinfo =
            (FAR struct fb_videoinfo_s *)((uintptr_t)arg);

          DEBUGASSERT(vinfo != 0 && fb->vtable != NULL &&
                      fb->vtable->getvideoinfo != NULL);
          ret = fb->vtable->getvideoinfo(fb->vtable, vinfo);
        }
        break;

      case FBIOGET_PLANEINFO:  /* Get video plane info */
        {
          FAR struct fb_planeinfo_s *pinfo =
            (FAR struct fb_planeinfo_s *)((uintptr_t)arg);

          DEBUGASSERT(pinfo != 0 && fb->vtable != NULL &&
                      fb->vtable->getplaneinfo != NULL);
          ret = fb->vtable->getplaneinfo(fb->vtable, fb->plane, pinfo);
        }
        break;

#ifdef CONFIG_FB_CMAP
      case FBIOGET_CMAP:       /* Get RGB color mapping */
        {
          FAR struct fb_cmap_s *cmap =
            (FAR struct fb_cmap_s *)((uintptr_t)arg);

          DEBUGASSERT(cmap != 0 && fb->vtable != NULL &&
                      fb->vtable->getcmap != NULL);
          ret = fb->vtable->getcmap(fb->vtable, cmap);
        }
        break;

      case FBIOPUT_CMAP:       /* Put RGB color mapping */
        {
          FAR const struct fb_cmap_s *cmap =
            (FAR struct fb_cmap_s *)((uintptr_t)arg);

          DEBUGASSERT(cmap != 0 && fb->vtable != NULL &&
                      fb->vtable->putcmap != NULL);
          ret = fb->vtable->putcmap(fb->vtable, cmap);
        }
        break;
#endif
#ifdef CONFIG_FB_HWCURSOR
      case FBIOGET_CURSOR:     /* Get cursor attributes */
        {
          FAR struct fb_cursorattrib_s *attrib =
            (FAR struct fb_cursorattrib_s *)((uintptr_t)arg);

          DEBUGASSERT(attrib != 0 && fb->vtable != NULL &&
                      fb->vtable->getcursor != NULL);
          ret = fb->vtable->getcursor(fb->vtable, attrib);
        }
        break;

      case FBIOPUT_CURSOR:     /* Set cursor attibutes */
        {
          FAR struct fb_setcursor_s *cursor =
            (FAR struct fb_setcursor_s *)((uintptr_t)arg);

          DEBUGASSERT(cursor != 0 && fb->vtable != NULL &&
                      fb->vtable->setcursor != NULL);
          ret = fb->vtable->setcursor(fb->vtable, cursor);
        }
        break;
#endif

#ifdef CONFIG_NX_UPDATE
      case FBIO_UPDATE:  /* Get video plane info */
        {
          FAR struct nxgl_rect_s *rect =
            (FAR struct nxgl_rect_s *)((uintptr_t)arg);
          struct fb_planeinfo_s pinfo;

          DEBUGASSERT(fb->vtable != NULL && fb->vtable->getplaneinfo != NULL);
          ret = fb->vtable->getplaneinfo(fb->vtable, fb->plane, &pinfo);
          if (ret >= 0)
            {
               nx_notify_rectangle((FAR NX_PLANEINFOTYPE *)&pinfo, rect);
            }
        }
        break;
#endif

      default:
        gerr("ERROR: Unsupported IOCTL command: %d\n", cmd);
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fb_register
 *
 * Description:
 *   Register the framebuffer character device at /dev/fbN where N is the
 *   display number if the devices supports only a single plane.  If the
 *   hardware supports multiple color planes, then the device will be
 *   registered at /dev/fbN-M where N is the again display number but M
 *   is the display plane.
 *
 * Input Parameters:
 *   display - The display number for the case of boards supporting multiple
 *             displays or for hardware that supports multiple
 *             layers (each layer is consider a display).  Typically zero.
 *   plane   - Identifies the color plane on hardware that supports separate
 *             framebuffer "planes" for each color component.
 *
 * Returned Value:
 *   Zero (OK) is returned success; a negated errno value is returned on any
 *   failure.
 *
 ****************************************************************************/

int fb_register(int display, int plane)
{
  FAR struct fb_chardev_s *fb;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  char devname[16];
  int nplanes;
  int ret;

  /* Allocate a framebuffer state instance */

  fb = (FAR struct fb_chardev_s *)kmm_zalloc(sizeof(struct fb_chardev_s));
  if (fb == NULL)
    {
      return -ENOMEM;
    }

  /* Initialize the frame buffer device. */

  ret = up_fbinitialize(display);
  if (ret < 0)
    {
      gerr("ERROR: up_fbinitialize() failed for display %d: %d\n", display, ret);
      goto errout_with_fb;
    }

  DEBUGASSERT((unsigned)plane <= UINT8_MAX);
  fb->plane  = plane;

  fb->vtable = up_fbgetvplane(display, plane);
  if (fb->vtable == NULL)
    {
      gerr("ERROR: up_fbgetvplane() failed, vplane=%d\n", plane);
      goto errout_with_fb;
    }

  /* Initialize the frame buffer instance. */

  DEBUGASSERT(fb->vtable->getvideoinfo != NULL);
  ret = fb->vtable->getvideoinfo(fb->vtable, &vinfo);
  if (ret < 0)
    {
      gerr("ERROR: getvideoinfo() failed: %d\n", ret);
      goto errout_with_fb;
    }

  nplanes = vinfo.nplanes;
  DEBUGASSERT(vinfo.nplanes > 0 && (unsigned)plane < vinfo.nplanes);

  DEBUGASSERT(fb->vtable->getplaneinfo != NULL);
  ret = fb->vtable->getplaneinfo(fb->vtable, plane, &pinfo);
  if (ret < 0)
    {
      gerr("ERROR: getplaneinfo() failed: %d\n", ret);
      goto errout_with_fb;
    }

  fb->fbmem  = pinfo.fbmem;
  fb->fblen  = pinfo.fblen;
  fb->bpp    = pinfo.bpp;

  /* Clear the framebuffer memory */

  memset(pinfo.fbmem, 0, pinfo.fblen);

  /* Register the framebuffer device */

  if (nplanes < 2)
    {
      (void)snprintf(devname, 16, "/dev/fb%d", display);
    }
  else
    {
      (void)snprintf(devname, 16, "/dev/fb%d-%d", display, plane);
    }

  ret = register_driver(devname, &fb_fops, 0666, (FAR void *)fb);
  if (ret < 0)
    {
      gerr("ERROR: register_driver() failed: %d\n", ret);
      goto errout_with_fb;
    }

  return OK;

errout_with_fb:
  kmm_free(fb);
  return ret;
}
