/* 7zFile.c -- File IO
2023-04-02 : Igor Pavlov : Public domain */

#include "Precomp.h"

#include "7zFile.h"

#ifdef GRACEOS_BFS

#include <stdlib.h>
#include <string.h>

#include "../../../drivers/storage/bfs.h"

extern struct bfs_instance g_bfs;

#define GRACEOS_OUT_GROW_MIN (1 << 12)

static WRes GraceFile_SetPath(CSzFile *p, const char *name)
{
  size_t len = strlen(name);
  char *path = (char *)malloc(len + 1);
  if (!path)
    return ENOMEM;
  memcpy(path, name, len + 1);
  if (p->path)
    free(p->path);
  p->path = path;
  return 0;
}

static WRes GraceFile_Load(CSzFile *p, const char *name)
{
  UInt64 file_size = 0;
  UInt64 out_size = 0;
  if (bfs_file_size(&g_bfs, name, &file_size) != 0)
    return ENOENT;

  if (file_size == 0)
  {
    p->data = NULL;
    p->size = 0;
    p->pos = 0;
    return 0;
  }

  if (file_size > (UInt64)(size_t)-1)
    return ENOMEM;

  p->data = (Byte *)malloc((size_t)file_size);
  if (!p->data)
    return ENOMEM;

  if (bfs_read_file(&g_bfs, name, p->data, file_size, &out_size) != 0 || out_size != file_size)
  {
    free(p->data);
    p->data = NULL;
    return EIO;
  }

  p->size = file_size;
  p->pos = 0;
  return 0;
}

static WRes GraceFile_EnsureCapacity(CSzFile *p, UInt64 needed)
{
  if (needed <= (UInt64)p->capacity)
    return 0;

  size_t new_cap = p->capacity ? p->capacity : GRACEOS_OUT_GROW_MIN;
  while ((UInt64)new_cap < needed)
  {
    if (new_cap > (size_t)-1 / 2)
    {
      new_cap = (size_t)needed;
      break;
    }
    new_cap *= 2;
  }

  {
    Byte *new_data = (Byte *)realloc(p->data, new_cap);
    if (!new_data)
      return ENOMEM;
    p->data = new_data;
    p->capacity = new_cap;
  }

  return 0;
}

void File_Construct(CSzFile *p)
{
  p->path = NULL;
  p->data = NULL;
  p->size = 0;
  p->pos = 0;
  p->capacity = 0;
  p->is_write = 0;
}

WRes InFile_Open(CSzFile *p, const char *name)
{
  WRes res;
  File_Construct(p);
  p->is_write = 0;
  res = GraceFile_SetPath(p, name);
  if (res != 0)
    return res;
  res = GraceFile_Load(p, name);
  if (res != 0)
  {
    if (p->data)
      free(p->data);
    if (p->path)
      free(p->path);
    File_Construct(p);
  }
  return res;
}

WRes OutFile_Open(CSzFile *p, const char *name)
{
  WRes res;
  File_Construct(p);
  p->is_write = 1;
  res = GraceFile_SetPath(p, name);
  if (res != 0)
    return res;
  p->size = 0;
  p->pos = 0;
  return 0;
}

WRes File_Close(CSzFile *p)
{
  WRes res = 0;
  if (p->is_write && p->path)
  {
    int rc = bfs_write_file(&g_bfs, p->path, p->data, p->size);
    if (rc != 0)
      res = (rc == -2 || rc == -3) ? ENOSPC : EIO;
  }

  if (p->data)
    free(p->data);
  if (p->path)
    free(p->path);

  File_Construct(p);
  return res;
}

WRes File_Read(CSzFile *p, void *data, size_t *size)
{
  size_t originalSize = *size;
  *size = 0;
  if (originalSize == 0)
    return 0;

  if (p->pos > p->size)
    return EINVAL;

  if (!p->data)
  {
    if (p->size == 0)
      return 0;
    return EBADF;
  }

  {
    UInt64 remaining = p->size - p->pos;
    size_t to_read = originalSize;
    if ((UInt64)to_read > remaining)
      to_read = (size_t)remaining;
    if (to_read == 0)
      return 0;

    memcpy(data, p->data + p->pos, to_read);
    p->pos += to_read;
    *size = to_read;
  }

  return 0;
}

WRes File_Write(CSzFile *p, const void *data, size_t *size)
{
  size_t originalSize = *size;
  *size = 0;
  if (originalSize == 0)
    return 0;

  if (!p->is_write)
    return EBADF;

  if ((UInt64)originalSize > (UInt64)(-1) - p->pos)
    return EINVAL;

  {
    UInt64 end_pos = p->pos + (UInt64)originalSize;
    WRes res = GraceFile_EnsureCapacity(p, end_pos);
    if (res != 0)
      return res;

    memcpy(p->data + p->pos, data, originalSize);
    p->pos = end_pos;
    if (p->pos > p->size)
      p->size = p->pos;
    *size = originalSize;
  }

  return 0;
}

WRes File_Seek(CSzFile *p, Int64 *pos, ESzSeek origin)
{
  Int64 new_pos;
  switch ((int)origin)
  {
    case SZ_SEEK_SET: new_pos = *pos; break;
    case SZ_SEEK_CUR: new_pos = (Int64)p->pos + *pos; break;
    case SZ_SEEK_END: new_pos = (Int64)p->size + *pos; break;
    default: return EINVAL;
  }

  if (new_pos < 0)
    return EINVAL;
  if ((UInt64)new_pos > p->size)
    return EINVAL;

  p->pos = (UInt64)new_pos;
  *pos = new_pos;
  return 0;
}

WRes File_GetLength(CSzFile *p, UInt64 *length)
{
  if (!length)
    return EINVAL;
  *length = p->size;
  return 0;
}

#else

#ifndef USE_WINDOWS_FILE

  #include <errno.h>

  #ifndef USE_FOPEN
    #include <stdio.h>
    #include <fcntl.h>
    #ifdef _WIN32
      #include <io.h>
      typedef int ssize_t;
      typedef int off_t;
    #else
      #include <unistd.h>
    #endif
  #endif

#else

/*
   ReadFile and WriteFile functions in Windows have BUG:
   If you Read or Write 64MB or more (probably min_failure_size = 64MB - 32KB + 1)
   from/to Network file, it returns ERROR_NO_SYSTEM_RESOURCES
   (Insufficient system resources exist to complete the requested service).
   Probably in some version of Windows there are problems with other sizes:
   for 32 MB (maybe also for 16 MB).
   And message can be "Network connection was lost"
*/

#endif

#define kChunkSizeMax (1 << 22)

void File_Construct(CSzFile *p)
{
  #ifdef USE_WINDOWS_FILE
  p->handle = INVALID_HANDLE_VALUE;
  #elif defined(USE_FOPEN)
  p->file = NULL;
  #else
  p->fd = -1;
  #endif
}

#if !defined(UNDER_CE) || !defined(USE_WINDOWS_FILE)

static WRes File_Open(CSzFile *p, const char *name, int writeMode)
{
  #ifdef USE_WINDOWS_FILE
  
  p->handle = CreateFileA(name,
      writeMode ? GENERIC_WRITE : GENERIC_READ,
      FILE_SHARE_READ, NULL,
      writeMode ? CREATE_ALWAYS : OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, NULL);
  return (p->handle != INVALID_HANDLE_VALUE) ? 0 : GetLastError();
  
  #elif defined(USE_FOPEN)
  
  p->file = fopen(name, writeMode ? "wb+" : "rb");
  return (p->file != 0) ? 0 :
    #ifdef UNDER_CE
    2; /* ENOENT */
    #else
    errno;
    #endif
  
  #else

  int flags = (writeMode ? (O_CREAT | O_EXCL | O_WRONLY) : O_RDONLY);
  #ifdef O_BINARY
  flags |= O_BINARY;
  #endif
  p->fd = open(name, flags, 0666);
  return (p->fd != -1) ? 0 : errno;

  #endif
}

WRes InFile_Open(CSzFile *p, const char *name) { return File_Open(p, name, 0); }

WRes OutFile_Open(CSzFile *p, const char *name)
{
  #if defined(USE_WINDOWS_FILE) || defined(USE_FOPEN)
  return File_Open(p, name, 1);
  #else
  p->fd = creat(name, 0666);
  return (p->fd != -1) ? 0 : errno;
  #endif
}

#endif


#ifdef USE_WINDOWS_FILE
static WRes File_OpenW(CSzFile *p, const WCHAR *name, int writeMode)
{
  p->handle = CreateFileW(name,
      writeMode ? GENERIC_WRITE : GENERIC_READ,
      FILE_SHARE_READ, NULL,
      writeMode ? CREATE_ALWAYS : OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, NULL);
  return (p->handle != INVALID_HANDLE_VALUE) ? 0 : GetLastError();
}
WRes InFile_OpenW(CSzFile *p, const WCHAR *name) { return File_OpenW(p, name, 0); }
WRes OutFile_OpenW(CSzFile *p, const WCHAR *name) { return File_OpenW(p, name, 1); }
#endif

WRes File_Close(CSzFile *p)
{
  #ifdef USE_WINDOWS_FILE
  
  if (p->handle != INVALID_HANDLE_VALUE)
  {
    if (!CloseHandle(p->handle))
      return GetLastError();
    p->handle = INVALID_HANDLE_VALUE;
  }
  
  #elif defined(USE_FOPEN)

  if (p->file != NULL)
  {
    int res = fclose(p->file);
    if (res != 0)
    {
      if (res == EOF)
        return errno;
      return res;
    }
    p->file = NULL;
  }

  #else

  if (p->fd != -1)
  {
    if (close(p->fd) != 0)
      return errno;
    p->fd = -1;
  }

  #endif

  return 0;
}


WRes File_Read(CSzFile *p, void *data, size_t *size)
{
  size_t originalSize = *size;
  *size = 0;
  if (originalSize == 0)
    return 0;

  #ifdef USE_WINDOWS_FILE

  do
  {
    const DWORD curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : (DWORD)originalSize;
    DWORD processed = 0;
    const BOOL res = ReadFile(p->handle, data, curSize, &processed, NULL);
    data = (void *)((Byte *)data + processed);
    originalSize -= processed;
    *size += processed;
    if (!res)
      return GetLastError();
    // debug : we can break here for partial reading mode
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #elif defined(USE_FOPEN)

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const size_t processed = fread(data, 1, curSize, p->file);
    data = (void *)((Byte *)data + (size_t)processed);
    originalSize -= processed;
    *size += processed;
    if (processed != curSize)
      return ferror(p->file);
    // debug : we can break here for partial reading mode
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #else

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const ssize_t processed = read(p->fd, data, curSize);
    if (processed == -1)
      return errno;
    if (processed == 0)
      break;
    data = (void *)((Byte *)data + (size_t)processed);
    originalSize -= (size_t)processed;
    *size += (size_t)processed;
    // debug : we can break here for partial reading mode
    // break;
  }
  while (originalSize > 0);

  #endif

  return 0;
}


WRes File_Write(CSzFile *p, const void *data, size_t *size)
{
  size_t originalSize = *size;
  *size = 0;
  if (originalSize == 0)
    return 0;
  
  #ifdef USE_WINDOWS_FILE

  do
  {
    const DWORD curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : (DWORD)originalSize;
    DWORD processed = 0;
    const BOOL res = WriteFile(p->handle, data, curSize, &processed, NULL);
    data = (const void *)((const Byte *)data + processed);
    originalSize -= processed;
    *size += processed;
    if (!res)
      return GetLastError();
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #elif defined(USE_FOPEN)

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const size_t processed = fwrite(data, 1, curSize, p->file);
    data = (void *)((Byte *)data + (size_t)processed);
    originalSize -= processed;
    *size += processed;
    if (processed != curSize)
      return ferror(p->file);
    if (processed == 0)
      break;
  }
  while (originalSize > 0);

  #else

  do
  {
    const size_t curSize = (originalSize > kChunkSizeMax) ? kChunkSizeMax : originalSize;
    const ssize_t processed = write(p->fd, data, curSize);
    if (processed == -1)
      return errno;
    if (processed == 0)
      break;
    data = (const void *)((const Byte *)data + (size_t)processed);
    originalSize -= (size_t)processed;
    *size += (size_t)processed;
  }
  while (originalSize > 0);

  #endif

  return 0;
}


WRes File_Seek(CSzFile *p, Int64 *pos, ESzSeek origin)
{
  #ifdef USE_WINDOWS_FILE

  DWORD moveMethod;
  UInt32 low = (UInt32)*pos;
  LONG high = (LONG)((UInt64)*pos >> 16 >> 16); /* for case when UInt64 is 32-bit only */
  // (int) to eliminate clang warning
  switch ((int)origin)
  {
    case SZ_SEEK_SET: moveMethod = FILE_BEGIN; break;
    case SZ_SEEK_CUR: moveMethod = FILE_CURRENT; break;
    case SZ_SEEK_END: moveMethod = FILE_END; break;
    default: return ERROR_INVALID_PARAMETER;
  }
  low = SetFilePointer(p->handle, (LONG)low, &high, moveMethod);
  if (low == (UInt32)0xFFFFFFFF)
  {
    WRes res = GetLastError();
    if (res != NO_ERROR)
      return res;
  }
  *pos = ((Int64)high << 32) | low;
  return 0;

  #else
  
  int moveMethod; // = origin;

  switch ((int)origin)
  {
    case SZ_SEEK_SET: moveMethod = SEEK_SET; break;
    case SZ_SEEK_CUR: moveMethod = SEEK_CUR; break;
    case SZ_SEEK_END: moveMethod = SEEK_END; break;
    default: return EINVAL;
  }
  
  #if defined(USE_FOPEN)
  {
    int res = fseek(p->file, (long)*pos, moveMethod);
    if (res == -1)
      return errno;
    *pos = ftell(p->file);
    if (*pos == -1)
      return errno;
    return 0;
  }
  #else
  {
    off_t res = lseek(p->fd, (off_t)*pos, moveMethod);
    if (res == -1)
      return errno;
    *pos = res;
    return 0;
  }
  
  #endif // USE_FOPEN
  #endif // USE_WINDOWS_FILE
}


WRes File_GetLength(CSzFile *p, UInt64 *length)
{
  #ifdef USE_WINDOWS_FILE
  
  DWORD sizeHigh;
  DWORD sizeLow = GetFileSize(p->handle, &sizeHigh);
  if (sizeLow == 0xFFFFFFFF)
  {
    DWORD res = GetLastError();
    if (res != NO_ERROR)
      return res;
  }
  *length = (((UInt64)sizeHigh) << 32) + sizeLow;
  return 0;
  
  #elif defined(USE_FOPEN)
  
  long pos = ftell(p->file);
  int res = fseek(p->file, 0, SEEK_END);
  *length = ftell(p->file);
  fseek(p->file, pos, SEEK_SET);
  return res;

  #else

  off_t pos;
  *length = 0;
  pos = lseek(p->fd, 0, SEEK_CUR);
  if (pos != -1)
  {
    const off_t len2 = lseek(p->fd, 0, SEEK_END);
    const off_t res2 = lseek(p->fd, pos, SEEK_SET);
    if (len2 != -1)
    {
      *length = (UInt64)len2;
      if (res2 != -1)
        return 0;
    }
  }
  return errno;
  
  #endif
}

#endif


/* ---------- FileSeqInStream ---------- */

static SRes FileSeqInStream_Read(ISeqInStreamPtr pp, void *buf, size_t *size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileSeqInStream)
  const WRes wres = File_Read(&p->file, buf, size);
  p->wres = wres;
  return (wres == 0) ? SZ_OK : SZ_ERROR_READ;
}

void FileSeqInStream_CreateVTable(CFileSeqInStream *p)
{
  p->vt.Read = FileSeqInStream_Read;
}


/* ---------- FileInStream ---------- */

static SRes FileInStream_Read(ISeekInStreamPtr pp, void *buf, size_t *size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileInStream)
  const WRes wres = File_Read(&p->file, buf, size);
  p->wres = wres;
  return (wres == 0) ? SZ_OK : SZ_ERROR_READ;
}

static SRes FileInStream_Seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileInStream)
  const WRes wres = File_Seek(&p->file, pos, origin);
  p->wres = wres;
  return (wres == 0) ? SZ_OK : SZ_ERROR_READ;
}

void FileInStream_CreateVTable(CFileInStream *p)
{
  p->vt.Read = FileInStream_Read;
  p->vt.Seek = FileInStream_Seek;
}


/* ---------- FileOutStream ---------- */

static size_t FileOutStream_Write(ISeqOutStreamPtr pp, const void *data, size_t size)
{
  Z7_CONTAINER_FROM_VTBL_TO_DECL_VAR_pp_vt_p(CFileOutStream)
  const WRes wres = File_Write(&p->file, data, &size);
  p->wres = wres;
  return size;
}

void FileOutStream_CreateVTable(CFileOutStream *p)
{
  p->vt.Write = FileOutStream_Write;
}
