/****************************************************************************
 * mm/circ_buf/circ_buf.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* Note about locking: There is no locking required while only one reader
 * and one writer is using the circular buffer.
 * For multiple writer and one reader there is only a need to lock the
 * writer. And vice versa for only one writer and multiple reader there is
 * only a need to lock the reader.
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mm/circ_buf.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: circ_buf_init
 *
 * Description:
 *   Initialize a circular buffer.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   base  - A pointer to circular buffer's internal buffer. It can be
 *           provided by caller because sometimes the creation of buffer
 *           is special or needs to preallocated, eg: DMA buffer.
 *           If NULL, a buffer of the given size will be allocated.
 *   bytes - The size of the internal buffer.
 *
 * Returned Value:
 *   Zero on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int circ_buf_init(FAR struct circ_buf_s *circ, FAR void *base, size_t bytes)
{
  if (!circ || (base && !bytes))
    {
      return -EINVAL;
    }

  circ->external = !!base;

  if (!base && bytes)
    {
      base = kmm_malloc(bytes);
      if (!base)
        {
          return -ENOMEM;
        }
    }

  circ->base = base;
  circ->size = bytes;
  circ->head = 0;
  circ->tail = 0;

  return 0;
}

/****************************************************************************
 * Name: circ_buf_resize
 *
 * Description:
 *   Resize a circular buffer (change buffer size).
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   bytes - The size of the internal buffer.
 *
 * Returned Value:
 *   Zero on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int circ_buf_resize(FAR struct circ_buf_s *circ, size_t bytes)
{
  FAR void *tmp;
  size_t len;

  if (!circ || circ->external)
    {
      return -EINVAL;
    }

  tmp = kmm_malloc(bytes);
  if (!tmp)
    {
      return -ENOMEM;
    }

  len = circ_buf_used(circ);
  if (bytes < len)
    {
      circ_buf_skip(circ, len - bytes);
      len = bytes;
    }

  circ_buf_read(circ, tmp, len);

  kmm_free(circ->base);

  circ->base = tmp;
  circ->size = bytes;
  circ->head = len;
  circ->tail = 0;

  return 0;
}

/****************************************************************************
 * Name: circ_buf_reset
 *
 * Description:
 *   Remove the entire circular buffer content.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

void circ_buf_reset(FAR struct circ_buf_s *circ)
{
  if (circ)
    {
      circ->head = circ->tail = 0;
    }
}

/****************************************************************************
 * Name: circ_buf_uninit
 *
 * Description:
 *   Free the circular buffer.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

void circ_buf_uninit(FAR struct circ_buf_s *circ)
{
  if (circ && !circ->external)
    {
      kmm_free(circ->base);
    }
}

/****************************************************************************
 * Name: circ_buf_size
 *
 * Description:
 *   Return size of the circular buffer.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

size_t circ_buf_size(FAR struct circ_buf_s *circ)
{
  if (circ)
    {
      return circ->size;
    }

  return 0;
}

/****************************************************************************
 * Name: circ_buf_used
 *
 * Description:
 *   Return the used bytes of the circular buffer.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

size_t circ_buf_used(FAR struct circ_buf_s *circ)
{
  if (circ)
    {
      return circ->head - circ->tail;
    }

  return 0;
}

/****************************************************************************
 * Name: circ_buf_space
 *
 * Description:
 *   Return the remaing space of the circular buffer.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

size_t circ_buf_space(FAR struct circ_buf_s *circ)
{
  return circ_buf_size(circ) - circ_buf_used(circ);
}

/****************************************************************************
 * Name: circ_buf_is_empty
 *
 * Description:
 *   Return true if the circular buffer is empty.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

bool circ_buf_is_empty(FAR struct circ_buf_s *circ)
{
  return !circ_buf_used(circ);
}

/****************************************************************************
 * Name: circ_buf_is_full
 *
 * Description:
 *   Return true if the circular buffer is full.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 ****************************************************************************/

bool circ_buf_is_full(FAR struct circ_buf_s *circ)
{
  return !circ_buf_space(circ);
}

/****************************************************************************
 * Name: circ_buf_peek
 *
 * Description:
 *   Get data form the circular buffer without removing
 *
 * Note :
 *   That with only one concurrent reader and one concurrent writer,
 *   you don't need extra locking to use these api.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   dst   - Address where to store the data.
 *   bytes - Number of bytes to get.
 *
 * Returned Value:
 *   The bytes of get data is returned if the peek data is successful;
 *   A negated errno value is returned on any failure.
 ****************************************************************************/

ssize_t circ_buf_peek(FAR struct circ_buf_s *circ,
                      FAR void *dst, size_t bytes)
{
  size_t len;
  size_t off;

  if (!circ)
    {
      return -EINVAL;
    }

  len = circ_buf_used(circ);
  off = circ->tail % circ->size;

  if (bytes > len)
    {
      bytes = len;
    }

  len = circ->size - off;
  if (bytes < len)
    {
      len = bytes;
    }

  memcpy(dst, circ->base + off, len);
  memcpy(dst + len, circ->base, bytes - len);

  return bytes;
}

/****************************************************************************
 * Name: circ_buf_read
 *
 * Description:
 *   Get data form the circular buffer.
 *
 * Note :
 *   That with only one concurrent reader and one concurrent writer,
 *   you don't need extra locking to use these api.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   dst   - Address where to store the data.
 *   bytes - Number of bytes to get.
 *
 * Returned Value:
 *   The bytes of get data is returned if the read data is successful;
 *   A negated errno value is returned on any failure.
 ****************************************************************************/

ssize_t circ_buf_read(FAR struct circ_buf_s *circ,
                      FAR void *dst, size_t bytes)
{
  if (!circ || !dst)
    {
      return -EINVAL;
    }

  bytes = circ_buf_peek(circ, dst, bytes);
  circ->tail += bytes;

  return bytes;
}

/****************************************************************************
 * Name: circ_buf_skip
 *
 * Description:
 *   Skip data form the circular buffer.
 *
 * Note :
 *   That with only one concurrent reader and one concurrent writer,
 *   you don't need extra locking to use these api.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   bytes - Number of bytes to skip.
 *
 * Returned Value:
 *   The bytes of get data is returned if the skip data is successful;
 *   A negated errno value is returned on any failure.
 ****************************************************************************/

ssize_t circ_buf_skip(FAR struct circ_buf_s *circ, size_t bytes)
{
  size_t len;

  if (!circ)
    {
      return -EINVAL;
    }

  len = circ_buf_used(circ);

  if (bytes > len)
    {
      bytes = len;
    }

  circ->tail += bytes;

  return bytes;
}

/****************************************************************************
 * Name: circ_buf_write
 *
 * Description:
 *   Write data to the circular buffer.
 *
 * Note :
 *   That with only one concurrent reader and one concurrent writer,
 *   you don't need extra locking to use these api.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   src   - The data to be added.
 *   bytes - Number of bytes to be added.
 *
 * Returned Value:
 *   The bytes of get data is returned if the write data is successful;
 *   A negated errno value is returned on any failure.
 ****************************************************************************/

ssize_t circ_buf_write(FAR struct circ_buf_s *circ,
                       FAR const void *src, size_t bytes)
{
  size_t space;
  size_t off;

  if (!circ || !src)
    {
      return -EINVAL;
    }

  space = circ_buf_space(circ);
  off = circ->head % circ->size;
  if (bytes > space)
    {
      bytes = space;
    }

  space = circ->size - off;
  if (bytes < space)
    {
      space = bytes;
    }

  memcpy(circ->base + off, src, space);
  memcpy(circ->base, src + space, bytes - space);
  circ->head += bytes;

  return bytes;
}

/****************************************************************************
 * Name: circ_buf_overwrite
 *
 * Description:
 *   Write data to the circular buffer. It can overwrite old data when
 *   circular buffer don't have enough space to store data.
 *
 * Note:
 *   Usage circ_buf_overwrite () is dangerous. It should be only called
 *   when the buffer is exclusived locked or when it is secured that no
 *   other thread is accessing the buffer.
 *
 * Input Parameters:
 *   circ  - Address of the circular buffer to be used.
 *   src   - The data to be added.
 *   bytes - Number of bytes to be added.
 *
 * Returned Value:
 *   The bytes length of overwrite is returned if it's successful;
 *   A negated errno value is returned on any failure.
 ****************************************************************************/

ssize_t circ_buf_overwrite(FAR struct circ_buf_s *circ,
                           FAR const void *src, size_t bytes)
{
  size_t overwrite = 0;
  size_t space;
  size_t off;

  if (!circ || !src)
    {
      return -EINVAL;
    }

  if (bytes > circ->size)
    {
      src += bytes - circ->size;
      bytes = circ->size;
    }

  space = circ_buf_space(circ);
  if (bytes > space)
    {
      overwrite = bytes - space;
    }

  off = circ->head % circ->size;
  space = circ->size - off;
  if (bytes < space)
    {
      space = bytes;
    }

  memcpy(circ->base + off, src, space);
  memcpy(circ->base, src + space, bytes - space);
  circ->head += bytes;
  circ->tail += overwrite;

  return overwrite;
}
