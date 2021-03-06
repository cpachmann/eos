// ----------------------------------------------------------------------
// File: proc/user/File.cc
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

#include "mgm/proc/ProcInterface.hh"
#include "mgm/XrdMgmOfs.hh"
#include "mgm/Access.hh"
#include "mgm/Macros.hh"
#include "mgm/Quota.hh"
#include "mgm/Stat.hh"
#include "mgm/TableFormatter/TableFormatterBase.hh"
#include "mgm/TableFormatter/TableCell.hh"
#include "common/LayoutId.hh"
#include "common/Path.hh"
#include "namespace/interface/IView.hh"
#include "namespace/interface/ContainerIterators.hh"
#include "namespace/Prefetcher.hh"
#include "namespace/Resolver.hh"
#include "namespace/utils/Etag.hh"
#include "namespace/utils/Checksum.hh"
#include <json/json.h>

EOSMGMNAMESPACE_BEGIN
//------------------------------------------------------------------------------
// Fileinfo method
//------------------------------------------------------------------------------
int
ProcCommand::Fileinfo()
{
  gOFS->MgmStats.Add("FileInfo", pVid->uid, pVid->gid, 1);
  XrdOucString spath = pOpaque->Get("mgm.path");
  const char* inpath = spath.c_str();
  NAMESPACEMAP;
  PROC_BOUNCE_ILLEGAL_NAMES;
  PROC_BOUNCE_NOT_ALLOWED;
  struct stat buf;
  unsigned long long fid = 0;

  if ((!spath.beginswith("fid:")) && (!spath.beginswith("fxid:")) &&
      (!spath.beginswith("pid:")) && (!spath.beginswith("pxid:")) &&
      (!spath.beginswith("inode:"))) {
    if (gOFS->_stat(path, &buf, *mError, *pVid, (char*) 0, (std::string*)0,
                    false)) {
      stdErr = "error: cannot stat '";
      stdErr += path;
      stdErr += "'\n";
      retc = ENOENT;
      return SFS_OK;
    }

    if (S_ISDIR(buf.st_mode)) {
      fid = buf.st_ino;
    } else {
      fid = eos::common::FileId::InodeToFid(buf.st_ino);
    }
  } else {
    XrdOucString sfid = spath;

    if ((sfid.replace("inode:", ""))) {
      fid = strtoull(sfid.c_str(), 0, 10);

      if (eos::common::FileId::IsFileInode(fid)) {
        buf.st_mode = S_IFREG;
        fid = eos::common::FileId::InodeToFid(fid);
        spath = "fid:";
        spath += eos::common::StringConversion::GetSizeString(sfid, fid);
        path = spath.c_str();
      } else {
        buf.st_mode = S_IFDIR;
        spath.replace("inode:", "pid:");
        path = spath.c_str();
      }
    } else { // one of fid, fxid, pid, pxid
      buf.st_mode = (sfid.beginswith("f")) ? S_IFREG : S_IFDIR;
      sfid.replace('p', 'f', 0, 1);
      fid = Resolver::retrieveFileIdentifier(sfid).getUnderlyingUInt64();
    }
  }

  if (mJsonFormat) {
    if (S_ISDIR(buf.st_mode)) {
      return DirJSON(fid, 0);
    } else {
      return FileJSON(fid, 0);
    }
  } else {
    if (S_ISDIR(buf.st_mode)) {
      return DirInfo(path);
    } else {
      return FileInfo(path);
    }
  }
}

//------------------------------------------------------------------------------
// Fileinfo given path
//------------------------------------------------------------------------------
int
ProcCommand::FileInfo(const char* path)
{
  XrdOucString option = pOpaque->Get("mgm.file.info.option");
  XrdOucString spath = path;
  uint64_t clock = 0;
  {
    eos::common::RWMutexReadLock viewReadLock;
    std::shared_ptr<eos::IFileMD> fmd;

    if ((spath.beginswith("fid:") || (spath.beginswith("fxid:")))) {
      unsigned long long fid = Resolver::retrieveFileIdentifier(
                                 spath).getUnderlyingUInt64();
      // reference by fid+fxid
      //-------------------------------------------
      eos::Prefetcher::prefetchFileMDAndWait(gOFS->eosView, fid);
      viewReadLock.Grab(gOFS->eosViewRWMutex);

      try {
        fmd = gOFS->eosFileService->getFileMD(fid, &clock);
        std::string fullpath = gOFS->eosView->getUri(fmd.get());
        spath = fullpath.c_str();
      } catch (eos::MDException& e) {
        errno = e.getErrno();
        stdErr = "error: cannot retrieve file meta data - ";
        stdErr += e.getMessage().str().c_str();
        eos_debug("caught exception %d %s\n", e.getErrno(),
                  e.getMessage().str().c_str());
      }
    } else {
      // reference by path
      //-------------------------------------------
      eos::Prefetcher::prefetchFileMDAndWait(gOFS->eosView, spath.c_str());
      viewReadLock.Grab(gOFS->eosViewRWMutex);

      try {
        fmd = gOFS->eosView->getFile(spath.c_str());
      } catch (eos::MDException& e) {
        errno = e.getErrno();
        stdErr = "error: cannot retrieve file meta data - ";
        stdErr += e.getMessage().str().c_str();
        eos_debug("caught exception %d %s\n", e.getErrno(),
                  e.getMessage().str().c_str());
      }
    }

    if (!fmd) {
      retc = errno;
      viewReadLock.Release();
      //-------------------------------------------
    } else {
      using eos::common::FileId;
      using eos::common::LayoutId;
      using eos::common::StringConversion;
      // Make a copy of the file metadata object
      std::shared_ptr<eos::IFileMD> fmd_copy(fmd->clone());
      fmd.reset();
      // TODO (esindril): All this copying should be reviewed
      viewReadLock.Release();
      //-------------------------------------------
      XrdOucString sizestring;
      XrdOucString hexfidstring;
      XrdOucString hexpidstring;
      bool Monitoring = false;
      bool Envformat = false;
      bool outputFilter = false;
      std::ostringstream out;
      FileId::Fid2Hex(fmd_copy->getId(), hexfidstring);
      FileId::Fid2Hex(fmd_copy->getContainerId(), hexpidstring);

      if ((option.find("-m")) != STR_NPOS) {
        Monitoring = true;
      }

      if ((option.find("-env")) != STR_NPOS) {
        Envformat = true;
        Monitoring = false;
      }

      if (Envformat) {
        std::string env;
        fmd_copy->getEnv(env);
        eos::common::Path cPath(spath.c_str());
        out << env << "&container=" << cPath.GetParentPath() << std::endl;
      } else {
        // Filter output according to requested filters
        // Note: filters affect only non-monitoring output
        if (!Monitoring) {
          if ((option.find("-path")) != STR_NPOS) {
            out << "path:   " << spath << std::endl;
          }

          if ((option.find("-fxid")) != STR_NPOS) {
            out << "fxid:   " << hexfidstring << std::endl;
          }

          if ((option.find("-fid")) != STR_NPOS) {
            out << "fid:    " << fmd_copy->getId() << std::endl;
          }

          if ((option.find("-size")) != STR_NPOS) {
            out << "size:   " << fmd_copy->getSize() << std::endl;
          }

          if ((option.find("-checksum")) != STR_NPOS) {
            std::string xs;
            eos::appendChecksumOnStringAsHex(fmd_copy.get(), xs);
            out << "xstype: " << LayoutId::GetChecksumString(fmd_copy->getLayoutId())
                << std::endl
                << "xs:     " << xs << std::endl;
          }

          // Mark filter flag if out is not empty
          outputFilter = (out.tellp() != std::streampos(0));
        }

        if (Monitoring || !outputFilter) {
          bool showFullpath = (option.find("-fullpath") != STR_NPOS);
          bool showProxygroup = (option.find("-proxy") != STR_NPOS);
          char ctimestring[4096];
          char mtimestring[4096];
          eos::IFileMD::ctime_t mtime;
          eos::IFileMD::ctime_t ctime;
          fmd_copy->getCTime(ctime);
          fmd_copy->getMTime(mtime);
          time_t filectime = (time_t) ctime.tv_sec;
          time_t filemtime = (time_t) mtime.tv_sec;
          std::string etag, xs_spaces;
          eos::calculateEtag(fmd_copy.get(), etag);
          eos::appendChecksumOnStringAsHex(fmd_copy.get(), xs_spaces, ' ');

          if (!Monitoring) {
            out << "  File: '" << spath << "'"
                << "  Flags: " << StringConversion::IntToOctal((int) fmd_copy->getFlags(), 4);

            if (clock) {
              out << "  Clock: " << FileId::Fid2Hex(clock);
            }

            out << std::endl;
            out << "  Size: " << fmd_copy->getSize() << std::endl;
            out << "Modify: " << ctime_r(&filemtime, mtimestring);
            out.seekp(-1, std::ios_base::end);
            out << " Timestamp: " << mtime.tv_sec << "." << mtime.tv_nsec
                << std::endl;
            out << "Change: " << ctime_r(&filectime, ctimestring);
            out.seekp(-1, std::ios_base::end);
            out << " Timestamp: " << ctime.tv_sec << "." << ctime.tv_nsec
                << std::endl;
            out << "  CUid: " << fmd_copy->getCUid()
                << " CGid: " << fmd_copy->getCGid()
                << "  Fxid: " << hexfidstring
                << " Fid: " << fmd_copy->getId()
                << "    Pid: " << fmd_copy->getContainerId()
                << "   Pxid: " << hexpidstring
                << std::endl;
            out << "XStype: " << LayoutId::GetChecksumString(fmd_copy->getLayoutId())
                << "    XS: " << xs_spaces
                << "    ETAGs: " << etag
                << std::endl;
            out << "Layout: " << LayoutId::GetLayoutTypeString(fmd_copy->getLayoutId())
                << " Stripes: " << (LayoutId::GetStripeNumber(fmd_copy->getLayoutId()) + 1)
                << " Blocksize: " << LayoutId::GetBlockSizeString(fmd_copy->getLayoutId())
                << " LayoutId: " << FileId::Fid2Hex(fmd_copy->getLayoutId())
                << std::endl;
            out << "  #Rep: " << fmd_copy->getNumLocation() << std::endl;
          } else {
            std::string xs;

            if (LayoutId::GetChecksumLen(fmd_copy->getLayoutId())) {
              eos::appendChecksumOnStringAsHex(fmd_copy.get(), xs);
            } else {
              xs = "0";
            }

            out << "keylength.file=" << spath.length()
                << " file=" << spath
                << " size=" << fmd_copy->getSize()
                << " mtime=" << mtime.tv_sec << "." << mtime.tv_nsec
                << " ctime=" << ctime.tv_sec << "." << ctime.tv_nsec
                << " clock=" << clock
                << " mode=" << StringConversion::IntToOctal((int) fmd_copy->getFlags(), 4)
                << " uid=" << fmd_copy->getCUid()
                << " gid=" << fmd_copy->getCGid()
                << " fxid=" << hexfidstring
                << " fid=" << fmd_copy->getId()
                << " ino=" << FileId::FidToInode(fmd_copy->getId())
                << " pid=" << fmd_copy->getContainerId()
                << " pxid=" << hexpidstring
                << " xstype=" << LayoutId::GetChecksumString(fmd_copy->getLayoutId())
                << " xs=" << xs
                << " etag=" << etag
                << " layout=" << LayoutId::GetLayoutTypeString(fmd_copy->getLayoutId())
                << " nstripes=" << (LayoutId::GetStripeNumber(fmd_copy->getLayoutId()) + 1)
                << " lid=" << FileId::Fid2Hex(fmd_copy->getLayoutId())
                << " nrep=" << fmd_copy->getNumLocation()
                << " ";
          }

          eos::IFileMD::LocationVector::const_iterator lociter;
          eos::IFileMD::LocationVector loc_vect = fmd_copy->getLocations();
          std::vector<unsigned int> selectedfs;
          std::vector<std::string> proxys;
          std::vector<std::string> firewalleps;
          std::vector<unsigned int> unavailfs;
          std::vector<unsigned int> replacedfs;
          size_t fsIndex;
          Scheduler::AccessArguments acsargs;
          int i = 0;
          int schedretc = -1;
          TableHeader table_mq_header;
          TableData table_mq_data;
          TableFormatterBase table_mq;
          bool table_mq_header_exist = false;

          for (lociter = loc_vect.begin(); lociter != loc_vect.end(); ++lociter) {
            // Ignore filesystem id 0
            if (!(*lociter)) {
              eos_err("fsid 0 found fid=%lld", fmd_copy->getId());
              continue;
            }

            XrdOucString location = "";
            location += (int) * lociter;
            eos::common::RWMutexReadLock lock(FsView::gFsView.ViewMutex);
            eos::common::FileSystem* filesystem = 0;

            if (FsView::gFsView.mIdView.count(*lociter)) {
              filesystem = FsView::gFsView.mIdView[*lociter];
            }

            if (filesystem) {
              // For the fullpath option we output the physical location of the
              // replicas
              XrdOucString fullpath;

              if (showFullpath) {
                FileId::FidPrefix2FullPath(hexfidstring.c_str(),
                                           filesystem->GetPath().c_str(),
                                           fullpath);
              }

              if (!Monitoring) {
                std::string format =
                  "header=1|key=host:width=24:format=s|key=schedgroup:width=16:format=s|key=path:width=16:format=s|key=stat.boot:width=10:format=s|key=configstatus:width=14:format=s|key=stat.drain:width=12:format=s|key=stat.active:width=8:format=s|key=stat.geotag:width=24:format=s";

                if (showProxygroup) {
                  format += "|key=proxygroup:width=24:format=s";
                }

                filesystem->Print(table_mq_header, table_mq_data, format);

                // Build header
                if (!table_mq_header.empty()) {
                  TableHeader table_mq_header_temp;
                  table_mq_header_temp.push_back(std::make_tuple("no.", 3, "-l"));
                  table_mq_header_temp.push_back(std::make_tuple("fs-id", 6, "l"));
                  std::copy(table_mq_header.begin(), table_mq_header.end(),
                            std::back_inserter(table_mq_header_temp));

                  if (showFullpath) {
                    table_mq_header_temp.push_back(std::make_tuple("physical location", 18, "s"));
                  }

                  table_mq.SetHeader(table_mq_header_temp);
                  table_mq_header_exist = true;
                }

                //Build body
                if (table_mq_header_exist) {
                  TableData table_mq_data_temp;

                  for (auto& row : table_mq_data) {
                    if (!row.empty()) {
                      table_mq_data_temp.emplace_back();
                      table_mq_data_temp.back().push_back(TableCell(i, "l"));
                      table_mq_data_temp.back().push_back(TableCell(*lociter, "l"));

                      for (auto& cell : row) {
                        table_mq_data_temp.back().push_back(cell);
                      }

                      if (showFullpath) {
                        table_mq_data_temp.back().push_back(TableCell(fullpath.c_str(), "s"));
                      }
                    }
                  }

                  table_mq.AddRows(table_mq_data_temp);
                  table_mq_data.clear();
                  table_mq_data_temp.clear();
                }

                if ((filesystem->GetString("proxygroup").size()) &&
                    (filesystem->GetString("proxygroup") != "<none>") &&
                    filesystem->GetString("filestickyproxydepth").size() &&
                    filesystem->GetLongLong("filestickyproxydepth") >= 0) {
                  // we do the scheduling only once when we meet a filesystem that requires it
                  if (schedretc == -1) {
                    acsargs.bookingsize = fmd_copy->getSize();
                    acsargs.dataproxys = &proxys;
                    acsargs.firewallentpts = NULL;
                    acsargs.forcedfsid = 0;
                    std::string space = filesystem->GetString("schedgroup");
                    space.resize(space.rfind("."));
                    acsargs.forcedspace = space.c_str();
                    acsargs.fsindex = &fsIndex;
                    acsargs.isRW = false;
                    acsargs.lid = fmd_copy->getLayoutId();
                    acsargs.inode = (ino64_t) fmd_copy->getId();
                    acsargs.locationsfs = &selectedfs;

                    for (auto it = loc_vect.begin(); it != loc_vect.end(); it++) {
                      selectedfs.push_back(*it);
                    }

                    std::string stried_cgi = "";
                    acsargs.tried_cgi = &stried_cgi;
                    acsargs.unavailfs = &unavailfs;
                    acsargs.vid = &vid;

                    if (!acsargs.isValid()) {
                      // there is something wrong in the arguments of file access
                      eos_static_err("open - invalid access argument");
                    }

                    schedretc = Quota::FileAccess(&acsargs);

                    if (schedretc) {
                      eos_static_warning("cannot schedule the proxy");
                    }
                  }

                  if (schedretc) {
                    out << "     sticky to undefined";
                  } else {
                    size_t k;

                    for (k = 0; k < loc_vect.size() && selectedfs[k] != loc_vect[i]; k++);

                    out << "sticky to " << proxys[k];
                  }
                }
              } else {
                out << "fsid=" << location << " ";

                if (showFullpath) {
                  out << "fullpath=" << fullpath << " ";
                }
              }
            } else {
              if (!Monitoring) {
                out << std::setw(3) << i << std::setw(8) << location
                    << " NA" << std::endl;
              }
            }

            i++;
          }

          out << table_mq.GenerateTable(HEADER);
          eos::IFileMD::LocationVector unlink_vect = fmd_copy->getUnlinkedLocations();

          for (lociter = unlink_vect.begin(); lociter != unlink_vect.end(); ++lociter) {
            if (!Monitoring) {
              out << "(undeleted) $ " << *lociter << std::endl;
            } else {
              out << "fsdel=" << *lociter << " ";
            }
          }

          if (!Monitoring) {
            out << "*******";
          }
        }
      }

      stdOut += out.str().c_str();
    }
  }
  return SFS_OK;
}

//------------------------------------------------------------------------------
// DirInfo by path
//------------------------------------------------------------------------------
int
ProcCommand::DirInfo(const char* path)
{
  XrdOucString option = pOpaque->Get("mgm.file.info.option");
  XrdOucString spath = path;
  uint64_t clock = 0;
  {
    eos::common::RWMutexReadLock viewReadLock;
    std::shared_ptr<eos::IContainerMD> dmd;

    if ((spath.beginswith("pid:") || (spath.beginswith("pxid:")))) {
      unsigned long long fid = 0;

      if (spath.beginswith("pid:")) {
        spath.replace("pid:", "");
        fid = strtoull(spath.c_str(), 0, 10);
      }

      if (spath.beginswith("pxid:")) {
        spath.replace("pxid:", "");
        fid = strtoull(spath.c_str(), 0, 16);
      }

      // reference by pid+pxid
      //-------------------------------------------
      viewReadLock.Grab(gOFS->eosViewRWMutex);

      try {
        dmd = gOFS->eosDirectoryService->getContainerMD(fid, &clock);
        std::string fullpath = gOFS->eosView->getUri(dmd.get());
        spath = fullpath.c_str();
      } catch (eos::MDException& e) {
        errno = e.getErrno();
        stdErr = "error: cannot retrieve directory meta data - ";
        stdErr += e.getMessage().str().c_str();
        eos_debug("caught exception %d %s\n", e.getErrno(),
                  e.getMessage().str().c_str());
      }
    } else {
      // reference by path
      //-------------------------------------------
      viewReadLock.Grab(gOFS->eosViewRWMutex);

      try {
        dmd = gOFS->eosView->getContainer(spath.c_str());
      } catch (eos::MDException& e) {
        errno = e.getErrno();
        stdErr = "error: cannot retrieve directory meta data - ";
        stdErr += e.getMessage().str().c_str();
        eos_debug("caught exception %d %s\n", e.getErrno(),
                  e.getMessage().str().c_str());
      }
    }

    if (!dmd) {
      retc = errno;
      viewReadLock.Release();
      //-------------------------------------------
    } else {
      using eos::common::FileId;
      using eos::common::LayoutId;
      using eos::common::StringConversion;
      size_t num_containers = dmd->getNumContainers();
      size_t num_files = dmd->getNumFiles();
      std::shared_ptr<eos::IContainerMD> dmd_copy(dmd->clone());
      dmd_copy->InheritChildren(*(dmd.get()));
      dmd.reset();
      viewReadLock.Release();
      //-------------------------------------------
      XrdOucString sizestring;
      XrdOucString hexfidstring;
      XrdOucString hexpidstring;
      bool Monitoring = false;
      bool outputFilter = false;
      std::ostringstream out;
      FileId::Fid2Hex(dmd_copy->getId(), hexfidstring);
      FileId::Fid2Hex(dmd_copy->getParentId(), hexpidstring);

      if ((option.find("-m")) != STR_NPOS) {
        Monitoring = true;
      }

      // Filter output according to requested filters
      // Note: filters affect only non-monitoring output
      if (!Monitoring) {
        if ((option.find("-path")) != STR_NPOS) {
          out << "path:   " << spath << std::endl;
        }

        if ((option.find("-fxid")) != STR_NPOS) {
          out << "fxid:   " << hexfidstring << std::endl;
        }

        if ((option.find("-fid")) != STR_NPOS) {
          out << "fid:    " << dmd_copy->getId() << std::endl;
        }

        if ((option.find("-size")) != STR_NPOS) {
          out << "size:   " << (num_containers + num_files) << std::endl;
        }

        outputFilter = ((out.tellp() != std::streampos(0)) ||
                        (option.find("-checksum") != STR_NPOS));
      }

      if (Monitoring || !outputFilter) {
        char ctimestring[4096];
        char mtimestring[4096];
        char tmtimestring[4096];
        eos::IContainerMD::ctime_t ctime;
        eos::IContainerMD::mtime_t mtime;
        eos::IContainerMD::tmtime_t tmtime;
        dmd_copy->getCTime(ctime);
        dmd_copy->getMTime(mtime);
        dmd_copy->getTMTime(tmtime);
        time_t filectime = (time_t) ctime.tv_sec;
        time_t filemtime = (time_t) mtime.tv_sec;
        time_t filetmtime = (time_t) tmtime.tv_sec;
        char fid[32];
        snprintf(fid, 32, "%llu", (unsigned long long) dmd_copy->getId());
        std::string etag;
        eos::calculateEtag(dmd_copy.get(), etag);

        if (!Monitoring) {
          out << "  Directory: '" << spath << "'"
              << "  Treesize: " << dmd_copy->getTreeSize() << std::endl;
          out << "  Container: " << num_containers
              << "  Files: " << num_files
              << "  Flags: " << StringConversion::IntToOctal(dmd_copy->getMode(), 4);

          if (clock) {
            out << "  Clock: " << FileId::Fid2Hex(clock);
          }

          out << std::endl;
          out << "Modify: " << ctime_r(&filemtime, mtimestring);
          out.seekp(-1, std::ios_base::end);
          out << " Timestamp: " << mtime.tv_sec << "." << mtime.tv_nsec
              << std::endl;
          out << "Change: " << ctime_r(&filectime, ctimestring);
          out.seekp(-1, std::ios_base::end);
          out << " Timestamp: " << ctime.tv_sec << "." << ctime.tv_nsec
              << std::endl;
          out << "Sync:   " << ctime_r(&filetmtime, tmtimestring);
          out.seekp(-1, std::ios_base::end);
          out << " Timestamp: " << tmtime.tv_sec << "." << tmtime.tv_nsec
              << std::endl;
          out << "  CUid: " << dmd_copy->getCUid()
              << " CGid: " << dmd_copy->getCGid()
              << "  Fxid: " << hexfidstring
              << " Fid: " << dmd_copy->getId()
              << "    Pid: " << dmd_copy->getParentId()
              << "   Pxid: " << hexpidstring
              << std::endl
              << "  ETAG: " << etag
              << std::endl;
        } else {
          out << "keylength.file=" << spath.length()
              << " file=" << spath
              << " treesize=" << dmd_copy->getTreeSize()
              << " container=" << num_containers
              << " files=" << num_files
              << " mtime=" << mtime.tv_sec << "." << mtime.tv_nsec
              << " ctime=" << ctime.tv_sec << "." << ctime.tv_nsec
              << " clock=" << clock
              << " mode=" << StringConversion::IntToOctal((int) dmd_copy->getMode(), 4)
              << " uid=" << dmd_copy->getCUid()
              << " gid=" << dmd_copy->getCGid()
              << " fxid=" << hexfidstring
              << " fid=" << dmd_copy->getId()
              << " ino=" << dmd_copy->getId()
              << " pid=" << dmd_copy->getParentId()
              << " pxid=" << hexpidstring
              << " etag=" << etag
              << " ";
          eos::IFileMD::XAttrMap xattrs = dmd_copy->getAttributes();

          for (const auto& elem : xattrs) {
            out << "xattrn=" << elem.first
                << " xattrv=" << elem.second << " ";
          }
        }
      }

      stdOut += out.str().c_str();
    }
  }
  return SFS_OK;
}

//------------------------------------------------------------------------------
// File info in JSON format
//------------------------------------------------------------------------------
int
ProcCommand::FileJSON(uint64_t fid, Json::Value* ret_json, bool dolock)
{
  eos::IFileMD::ctime_t ctime;
  eos::IFileMD::ctime_t mtime;
  eos_static_debug("fid=%llu", fid);
  Json::Value json;
  json["id"] = (Json::Value::UInt64) fid;
  XrdOucString hexstring;
  eos::common::FileId::Fid2Hex(fid, hexstring);

  try {
    eos::Prefetcher::prefetchFileMDAndWait(gOFS->eosView, fid);
    eos::common::RWMutexReadLock viewReadLock;

    if (dolock) {
      viewReadLock.Grab(gOFS->eosViewRWMutex);
    }

    std::shared_ptr<eos::IFileMD> fmd = gOFS->eosFileService->getFileMD(fid);
    std::string path = gOFS->eosView->getUri(fmd.get());
    std::shared_ptr<eos::IFileMD> fmd_copy(fmd->clone());
    fmd.reset();
    viewReadLock.Release();
    // TODO (esindril): All this copying should be reviewed
    //--------------------------------------------------------------------------
    fmd_copy->getCTime(ctime);
    fmd_copy->getMTime(mtime);
    unsigned long long nlink = (fmd_copy->isLink()) ? 1 :
                               fmd_copy->getNumLocation();
    json["fxid"] = hexstring.c_str();
    json["inode"] = (Json::Value::UInt64) eos::common::FileId::FidToInode(fid);
    json["ctime"] = (Json::Value::UInt64) ctime.tv_sec;
    json["ctime_ns"] = (Json::Value::UInt64) ctime.tv_nsec;
    json["atime"] = (Json::Value::UInt64) ctime.tv_sec;
    json["atime_ns"] = (Json::Value::UInt64) ctime.tv_nsec;
    json["mtime"] = (Json::Value::UInt64) mtime.tv_sec;
    json["mtime_ns"] = (Json::Value::UInt64) mtime.tv_nsec;
    json["size"] = (Json::Value::UInt64) fmd_copy->getSize();
    json["uid"] = fmd_copy->getCUid();
    json["gid"] = fmd_copy->getCGid();
    json["mode"] = fmd_copy->getFlags();
    json["nlink"] = (Json::Value::UInt64) nlink;
    json["name"] = fmd_copy->getName();
    json["path"] = path;
    json["layout"] = eos::common::LayoutId::GetLayoutTypeString(
                       fmd_copy->getLayoutId());
    json["nstripes"] = (int)(eos::common::LayoutId::GetStripeNumber(
                               fmd_copy->getLayoutId())
                             + 1);

    if (fmd_copy->isLink()) {
      json["target"] = fmd_copy->getLink();
    }

    Json::Value jsonxattr;
    eos::IFileMD::XAttrMap xattrs = fmd_copy->getAttributes();

    for (const auto& elem : xattrs) {
      jsonxattr[elem.first] = elem.second;
    }

    if (fmd_copy->numAttributes()) {
      json["xattr"] = jsonxattr;
    }

    Json::Value jsonfsids;
    eos::IFileMD::LocationVector loc_vect = fmd_copy->getLocations();

    // Get host name for the fs ids
    for (auto loc_it = loc_vect.begin(); loc_it != loc_vect.end(); ++loc_it) {
      eos::common::RWMutexReadLock lock(FsView::gFsView.ViewMutex);
      eos::common::FileSystem* filesystem = 0;
      Json::Value jsonfsinfo;

      if (FsView::gFsView.mIdView.count(*loc_it)) {
        filesystem = FsView::gFsView.mIdView[*loc_it];
      }

      if (filesystem) {
        eos::common::FileSystem::fs_snapshot_t fs;

        if (filesystem->SnapShotFileSystem(fs, true)) {
          XrdOucString fstpath;
          eos::common::FileId::FidPrefix2FullPath(hexstring.c_str(), fs.mPath.c_str(),
                                                  fstpath);
          jsonfsinfo["fsid"] = fs.mId;
          jsonfsinfo["geotag"] = fs.mGeoTag;
          jsonfsinfo["host"] = fs.mHost;
          jsonfsinfo["mountpoint"] = fs.mPath;
          jsonfsinfo["fstpath"] = fstpath.c_str();
          jsonfsinfo["schedgroup"] = fs.mGroup;
          jsonfsinfo["status"] = eos::common::FileSystem::GetStatusAsString(fs.mStatus);
          jsonfsids.append(jsonfsinfo);
        }
      }
    }

    json["locations"] = jsonfsids;
    json["checksumtype"] = eos::common::LayoutId::GetChecksumString(
                             fmd_copy->getLayoutId());
    std::string cks;
    eos::appendChecksumOnStringAsHex(fmd_copy.get(), cks);
    json["checksumvalue"] = cks;
    std::string etag;
    eos::calculateEtag(fmd_copy.get(), etag);
    json["etag"] = etag;
  } catch (eos::MDException& e) {
    errno = e.getErrno();
    eos_static_debug("caught exception %d %s\n", e.getErrno(),
                     e.getMessage().str().c_str());
    json["errc"] = errno;
    json["errmsg"] = e.getMessage().str().c_str();
  }

  if (!ret_json) {
    std::stringstream r;
    r << json;
    stdJson += r.str().c_str();
  } else {
    *ret_json = json;
  }

  retc = 0;
  return SFS_OK;
}

//------------------------------------------------------------------------------
// Get directory info in JSON format
//------------------------------------------------------------------------------
int
ProcCommand::DirJSON(uint64_t fid, Json::Value* ret_json, bool dolock)
{
  eos::IFileMD::ctime_t ctime;
  eos::IFileMD::ctime_t mtime;
  eos::IFileMD::ctime_t tmtime;
  eos_static_debug("fid=%llu", fid);
  Json::Value json;
  json["id"] = (Json::Value::UInt64) fid;

  try {
    eos::common::RWMutexReadLock viewReadLock;

    if (dolock) {
      viewReadLock.Grab(gOFS->eosViewRWMutex);
    }

    std::shared_ptr<eos::IContainerMD> cmd =
      gOFS->eosDirectoryService->getContainerMD(fid);
    std::string path = gOFS->eosView->getUri(cmd.get());
    cmd->getCTime(ctime);
    cmd->getMTime(mtime);
    cmd->getTMTime(tmtime);
    json["inode"] = (Json::Value::UInt64) fid;
    json["ctime"] = (Json::Value::UInt64) ctime.tv_sec;
    json["ctime_ns"] = (Json::Value::UInt64) ctime.tv_nsec;
    json["atime"] = (Json::Value::UInt64) ctime.tv_sec;
    json["atime_ns"] = (Json::Value::UInt64) ctime.tv_nsec;
    json["mtime"] = (Json::Value::UInt64) mtime.tv_sec;
    json["mtime_ns"] = (Json::Value::UInt64) mtime.tv_nsec;
    json["tmtime"] = (Json::Value::UInt64) tmtime.tv_sec;
    json["tmtime_ns"] = (Json::Value::UInt64) tmtime.tv_nsec;
    json["treesize"] = (Json::Value::UInt64) cmd->getTreeSize();
    json["uid"] = cmd->getCUid();
    json["gid"] = cmd->getCGid();
    json["mode"] = cmd->getFlags();
    json["nlink"] = 1;
    json["name"] = cmd->getName();
    json["path"] = path;
    json["nndirectories"] = (int)cmd->getNumContainers();
    json["nfiles"] = (int)cmd->getNumFiles();
    Json::Value chld;
    std::shared_ptr<eos::IContainerMD> cmd_copy(cmd->clone());
    cmd_copy->InheritChildren(*(cmd.get()));
    cmd.reset();
    viewReadLock.Release();

    if (!ret_json) {
      for (auto it = FileMapIterator(cmd_copy); it.valid(); it.next()) {
        Json::Value fjson;
        FileJSON(it.value(), &fjson, true);
        chld.append(fjson);
      }

      // Loop through all subcontainers
      for (auto dit = ContainerMapIterator(cmd_copy); dit.valid(); dit.next()) {
        Json::Value djson;
        DirJSON(dit.value(), &djson, true);
        chld.append(djson);
      }
    }

    if ((cmd_copy->getNumFiles() + cmd_copy->getNumContainers()) != 0) {
      json["children"] = chld;
    }

    Json::Value jsonxattr;
    eos::IFileMD::XAttrMap xattrs = cmd_copy->getAttributes();

    for (const auto& elem : xattrs) {
      jsonxattr[elem.first] = elem.second;
    }

    if (cmd_copy->numAttributes()) {
      json["xattr"] = jsonxattr;
    }

    std::string etag;
    eos::calculateEtag(cmd_copy.get(), etag);
    json["etag"] = etag;
  } catch (eos::MDException& e) {
    errno = e.getErrno();
    eos_static_debug("caught exception %d %s\n", e.getErrno(),
                     e.getMessage().str().c_str());
    json["errc"] = errno;
    json["errmsg"] = e.getMessage().str().c_str();
  }

  if (!ret_json) {
    std::stringstream r;
    r << json;
    stdJson += r.str().c_str();
    retc = 0;
  } else {
    *ret_json = json;
  }

  return SFS_OK;
}

EOSMGMNAMESPACE_END
