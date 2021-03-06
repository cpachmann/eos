// ----------------------------------------------------------------------
// File: FileId.hh
// Author: Andreas-Joachim Peters - CERN
// ----------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2011 CERN/Switzerland                                  *
 *                                                                      *
 * This program is free software: you can redistribute it and/or modify *
 * it under the terms of the GNU General Public License as published by *
 * the Free Software Foundation, either version 3 of the License, or    *
 * (at your option) any later version.                                  *
 *                                                                      *
 * This program is distributed in the hope that it will be useful,      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of       *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
 * GNU General Public License for more details.                         *
 *                                                                      *
 * You should have received a copy of the GNU General Public License    *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 ************************************************************************/

#pragma once
#include "common/Namespace.hh"
#include "XrdOuc/XrdOucString.hh"

EOSCOMMONNAMESPACE_BEGIN

//------------------------------------------------------------------------------
//! Class to handle file IDs.
//! Provides conversion functions from/to hex representation and to build path
//! names from fids and prefixes
//------------------------------------------------------------------------------
class FileId
{
public:
  typedef unsigned long long fileid_t;

  //----------------------------------------------------------------------------
  //! Convert a fid into a hexadecimal string
  //----------------------------------------------------------------------------
  static std::string Fid2Hex(unsigned long long fid)
  {
    char hexbuffer[128];
    sprintf(hexbuffer, "%08llx", fid);
    return std::string(hexbuffer);
  }

  //----------------------------------------------------------------------------
  //! Convert a fid into a hexadecimal string
  //----------------------------------------------------------------------------
  static void Fid2Hex(unsigned long long fid, XrdOucString& hexstring)
  {
    char hexbuffer[128];
    sprintf(hexbuffer, "%08llx", fid);
    hexstring = hexbuffer;
  }

  //----------------------------------------------------------------------------
  //! Convert a hex decimal string into a fid
  //----------------------------------------------------------------------------
  static unsigned long long Hex2Fid(const char* hexstring)
  {
    if (hexstring) {
      return strtoll(hexstring, 0, 16);
    } else {
      return 0;
    }
  }

  //----------------------------------------------------------------------------
  //! Convert an EOS file id into an inode number. Currently dispatches to the
  //! legacy implementation.
  //----------------------------------------------------------------------------
  static unsigned long long FidToInode(unsigned long long fid)
  {
    return LegacyFidToInode(fid);
  }

  static unsigned long long InodeToFid(unsigned long long ino)
  {
    if(NewIsFileInode(ino)) {
      return NewInodeToFid(ino);
    }

    return LegacyInodeToFid(ino);
  }

  static bool IsFileInode(unsigned long long ino)
  {
    return LegacyIsFileInode(ino);
  }

  //----------------------------------------------------------------------------
  //! New encoding - convert an EOS file id into an inode number.
  //! We mark the last bit with "1" for files, and "0" for containers.
  //----------------------------------------------------------------------------
  static constexpr unsigned long long kLastBitSet = (1ull << 63);

  static unsigned long long NewFidToInode(unsigned long long fid)
  {
    return fid | kLastBitSet;
  }

  static unsigned long long NewInodeToFid(unsigned long long ino)
  {
    return ino & (~kLastBitSet);
  }

  static bool NewIsFileInode(unsigned long long ino)
  {
    return (ino & kLastBitSet) != 0ull;
  }

  //----------------------------------------------------------------------------
  //! Legacy encoding - convert an EOS file id into an inode number - we shift
  //! the range by 28 bytes to not overlap with directory inodes.
  //----------------------------------------------------------------------------
  static unsigned long long LegacyFidToInode(unsigned long long fid)
  {
    return (fid << 28);
  }

  static unsigned long long LegacyInodeToFid(unsigned long long ino)
  {
    return (ino >> 28);
  }

  static bool LegacyIsFileInode(unsigned long long ino)
  {
    return (ino >= (1 << 28));
  }

  //----------------------------------------------------------------------------
  //! Compute a path from a fid and localprefix
  //----------------------------------------------------------------------------
  static void FidPrefix2FullPath(const char* hexstring, const char* localprefix,
                                 XrdOucString& fullpath, unsigned int subindex = 0)
  {
    if ((!hexstring) || (!localprefix)) {
      fullpath = "";
      return;
    }

    unsigned long long fid = Hex2Fid(hexstring);
    char sfullpath[16384];
    XrdOucString slocalprefix = localprefix;

    if (!slocalprefix.endswith("/")) {
      slocalprefix += "/";
    }

    if (subindex) {
      sprintf(sfullpath, "%s%08llx/%s.%u", slocalprefix.c_str(), fid / 10000,
              hexstring, subindex);
    } else {
      sprintf(sfullpath, "%s%08llx/%s", slocalprefix.c_str(), fid / 10000, hexstring);
    }

    fullpath = sfullpath;
  }

  //----------------------------------------------------------------------------
  //! Compute a fid from a prefix path
  //----------------------------------------------------------------------------
  static unsigned long long PathToFid(const char* path)
  {
    XrdOucString hexfid = "";
    hexfid = path;
    int rpos = hexfid.rfind("/");

    if (rpos > 0) {
      hexfid.erase(0, rpos + 1);
    }

    return Hex2Fid(hexfid.c_str());
  }
};

EOSCOMMONNAMESPACE_END
