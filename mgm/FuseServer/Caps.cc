//------------------------------------------------------------------------------
// File: FuseServer/Caps.cc
// Author: Andreas-Joachim Peters - CERN
//------------------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2019 CERN/Switzerland                                  *
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

#include <string>
#include <cstdlib>

#include "mgm/FuseServer/Caps.hh"
#include <thread>
#include <regex>

#include "common/Logging.hh"
#include "common/Timing.hh"

#include "mgm/ZMQ.hh"
#include "mgm/XrdMgmOfs.hh"
#include "mgm/Stat.hh"

#include "namespace/interface/IView.hh"

EOSMGMNAMESPACE_BEGIN

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void
FuseServer::Caps::Store(const eos::fusex::cap& ecap,
                        eos::common::Mapping::VirtualIdentity* vid)
{
  gOFS->MgmStats.Add("Eosxd::int::Store", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::Store");
  eos::common::RWMutexWriteLock lLock(*this);
  eos_static_info("id=%lx clientid=%s authid=%s",
                  ecap.id(),
                  ecap.clientid().c_str(),
                  ecap.authid().c_str());

  // avoid to have multiple time entries for the same cap
  if (!mCaps.count(ecap.authid())) {
    // fill the three views on caps
    mTimeOrderedCap.insert(std::pair<time_t, authid_t>(ecap.vtime(),
                           ecap.authid()));
  }

  mClientCaps[ecap.clientid()].insert(ecap.authid());
  mClientInoCaps[ecap.clientid()][ecap.id()].insert(ecap.authid());
  shared_cap cap = std::make_shared<capx>();
  *cap = ecap;
  cap->set_vid(vid);
  mCaps[ecap.authid()] = cap;
  mInodeCaps[ecap.id()].insert(ecap.authid());
  EXEC_TIMING_END("Eosxd::int::Store");
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool
FuseServer::Caps::Imply(uint64_t md_ino,
                        FuseServer::Caps::authid_t authid,
                        FuseServer::Caps::authid_t implied_authid)
{
  eos_static_info("id=%lx authid=%s implied-authid=%s",
                  md_ino,
                  authid.c_str(),
                  implied_authid.c_str());
  shared_cap implied_cap = std::make_shared<capx>();
  shared_cap cap = Get(authid);

  if (!cap->id() || !implied_authid.length()) {
    return false;
  }

  *implied_cap = *cap;
  implied_cap->set_authid(implied_authid);
  implied_cap->set_id(md_ino);
  implied_cap->set_vid(cap->vid());
  struct timespec ts;
  eos::common::Timing::GetTimeSpec(ts, true);
  {
    size_t leasetime = 0;
    {
      eos::common::RWMutexReadLock lLock(gOFS->zMQ->gFuseServer.Client());
      leasetime = gOFS->zMQ->gFuseServer.Client().leasetime(cap->clientuuid());
    }
    eos::common::RWMutexWriteLock lock(*this);
    implied_cap->set_vtime(ts.tv_sec + (leasetime ? leasetime : 300));
    implied_cap->set_vtime_ns(ts.tv_nsec);
    // fill the three views on caps
    mTimeOrderedCap.insert(std::pair<time_t, authid_t>(implied_cap->vtime(),
                           implied_authid));
    mClientCaps[cap->clientid()].insert(implied_authid);
    mClientInoCaps[cap->clientid()][cap->id()].insert(implied_authid);
    mCaps[implied_authid] = implied_cap;
    mInodeCaps[md_ino].insert(implied_authid);
  }
  return true;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
FuseServer::Caps::shared_cap
FuseServer::Caps::Get(FuseServer::Caps::authid_t id)
{
  if (mCaps.count(id)) {
    return mCaps[id];
  } else {
    return std::make_shared<capx>();
  }
}

FuseServer::Caps::shared_cap
FuseServer::Caps::GetTS(FuseServer::Caps::authid_t id)
{
  eos::common::RWMutexWriteLock lLock(*this);

  if (mCaps.count(id)) {
    return mCaps[id];
  } else {
    return std::make_shared<capx>();
  }
}



/*----------------------------------------------------------------------------*/
int
FuseServer::Caps::BroadcastReleaseFromExternal(uint64_t id)
/*----------------------------------------------------------------------------*/
{
  gOFS->MgmStats.Add("Eosxd::int::BcReleaseExt", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcReleaseExt");
  // broad-cast release for a given inode
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx ",
                  id);
  std::vector<shared_cap> bccaps;

  if (mInodeCaps.count(id)) {
    for (auto it = mInodeCaps[id].begin();
         it != mInodeCaps[id].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      if (cap->id()) {
        bccaps.push_back(cap);
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().ReleaseCAP((uint64_t) it->id(),
        it->clientuuid(),
        it->clientid());
    errno = 0 ; // seems that ZMQ function might set errno
  }

  EXEC_TIMING_END("Eosxd::int::BcReleaseExt");
  return 0;
}

/*----------------------------------------------------------------------------*/
int
FuseServer::Caps::BroadcastRefreshFromExternal(uint64_t id, uint64_t pid)
/*----------------------------------------------------------------------------*/
{
  gOFS->MgmStats.Add("Eosxd::int::BcRefreshExt", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcRefreshExt");
  // broad-cast refresh for a given inode
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx pid=%lx",
                  id,
                  pid);
  std::vector<shared_cap> bccaps;

  if (mInodeCaps.count(pid)) {
    for (auto it = mInodeCaps[pid].begin();
         it != mInodeCaps[pid].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      if (cap->id()) {
        bccaps.push_back(cap);
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().RefreshEntry((uint64_t) id,
        it->clientuuid(),
        it->clientid());
    errno = 0 ; // seems that ZMQ function might set errno
  }

  EXEC_TIMING_END("Eosxd::int::BcRefreshExt");
  return 0;
}

int
FuseServer::Caps::BroadcastRelease(const eos::fusex::md& md)
{
  gOFS->MgmStats.Add("Eosxd::int::BcRelease", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcRelease");
  FuseServer::Caps::shared_cap refcap = Get(md.authid());
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx/%lx clientid=%s clientuuid=%s authid=%s",
                  refcap->id(),
                  md.md_pino(),
                  refcap->clientid().c_str(),
                  refcap->clientuuid().c_str(),
                  refcap->authid().c_str());
  std::vector<shared_cap> bccaps;
  uint64_t md_pino = refcap->id();

  if (!md_pino) {
    md_pino = md.md_pino();
  }

  if (mInodeCaps.count(md_pino)) {
    for (auto it = mInodeCaps[md_pino].begin();
         it != mInodeCaps[md_pino].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      // skip our own cap!
      if (cap->authid() == md.authid()) {
        continue;
      }

      // skip identical client mounts!
      if (cap->clientuuid() == refcap->clientuuid()) {
        continue;
      }

      // skip same source
      if (cap->clientuuid() == md.clientuuid()) {
        continue;
      }

      if (cap->id()) {
        bccaps.push_back(cap);
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().ReleaseCAP((uint64_t) it->id(),
        it->clientuuid(),
        it->clientid());
    errno = 0 ;
  }

  EXEC_TIMING_END("Eosxd::int::BcRelease");
  return 0;
}

/*----------------------------------------------------------------------------*/
int
FuseServer::Caps::BroadcastDeletionFromExternal(uint64_t id,
    const std::string& name)
/*----------------------------------------------------------------------------*/
{
  gOFS->MgmStats.Add("Eosxd::int::BcDeletionExt", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcDeletionExt");
  // broad-cast deletion for a given name in a container
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx name=%s",
                  id,
                  name.c_str());
  std::vector<shared_cap> bccaps;

  if (mInodeCaps.count(id)) {
    for (auto it = mInodeCaps[id].begin();
         it != mInodeCaps[id].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      if (cap->id()) {
        bccaps.push_back(cap);
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().DeleteEntry((uint64_t) it->id(),
        it->clientuuid(),
        it->clientid(),
        name);
    errno = 0 ; // seems that ZMQ function might set errno
  }

  EXEC_TIMING_END("Eosxd::int::BcDeletionExt");
  return 0;
}

/*----------------------------------------------------------------------------*/
int
FuseServer::Caps::BroadcastDeletion(uint64_t id, const eos::fusex::md& md,
                                    const std::string& name)
/*----------------------------------------------------------------------------*/
{
  gOFS->MgmStats.Add("Eosxd::int::BcDeletion", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcDeletion");
  FuseServer::Caps::shared_cap refcap = Get(md.authid());
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx name=%s",
                  id,
                  name.c_str());
  std::vector<shared_cap> bccaps;

  if (mInodeCaps.count(refcap->id())) {
    for (auto it = mInodeCaps[refcap->id()].begin();
         it != mInodeCaps[refcap->id()].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      // skip our own cap!
      if (cap->authid() == refcap->authid()) {
        continue;
      }

      // skip identical client mounts!
      if (cap->clientuuid() == refcap->clientuuid()) {
        continue;
      }

      // skip same source
      if (cap->clientuuid() == md.clientuuid()) {
        continue;
      }

      if (cap->id()) {
        bccaps.push_back(cap);
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().DeleteEntry((uint64_t) it->id(),
        it->clientuuid(),
        it->clientid(),
        name);
    errno = 0;
  }

  EXEC_TIMING_END("Eosxd::int::BcDeletion");
  return 0;
}

/*----------------------------------------------------------------------------*/
int
FuseServer::Caps::BroadcastRefresh(uint64_t inode,
                                   const eos::fusex::md& md,
                                   uint64_t parent_inode)
/*----------------------------------------------------------------------------*/
{
  gOFS->MgmStats.Add("Eosxd::int::BcRefresh", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcRefresh");
  FuseServer::Caps::shared_cap refcap = Get(md.authid());
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx parent=%lx",
                  inode,
                  parent_inode);
  std::vector<shared_cap> bccaps;

  if (mInodeCaps.count(parent_inode)) {
    for (auto it = mInodeCaps[parent_inode].begin();
         it != mInodeCaps[parent_inode].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      // skip identical client mounts!
      if (cap->clientuuid() == refcap->clientuuid()) {
        continue;
      }

      // skip same source
      if (cap->clientuuid() == md.clientuuid()) {
        continue;
      }

      if (cap->id()) {
        bccaps.push_back(cap);
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().RefreshEntry((uint64_t) inode,
        it->clientuuid(),
        it->clientid()
                                                );
    errno = 0;
  }

  EXEC_TIMING_END("Eosxd::int::BcRefresh");
  return 0;
}


int
FuseServer::Caps::BroadcastCap(shared_cap cap)
{
  if (cap && cap->id()) {
    (void) gOFS->zMQ->gFuseServer.Client().SendCAP(cap);
  }

  return -1;
}

int
FuseServer::Caps::BroadcastMD(const eos::fusex::md& md,
                              uint64_t md_ino,
                              uint64_t md_pino,
                              uint64_t clock,
                              struct timespec& p_mtime)
{
  gOFS->MgmStats.Add("Eosxd::int::BcMD", 0, 0, 1);
  EXEC_TIMING_BEGIN("Eosxd::int::BcMD");
  FuseServer::Caps::shared_cap refcap = Get(md.authid());
  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("id=%lx/%lx clientid=%s clientuuid=%s authid=%s",
                  refcap->id(),
                  md_pino,
                  refcap->clientid().c_str(),
                  refcap->clientuuid().c_str(),
                  refcap->authid().c_str());
  std::set<std::string> clients_sent;
  std::vector<shared_cap> bccaps;

  if (mInodeCaps.count(md_pino)) {
    for (auto it = mInodeCaps[md_pino].begin();
         it != mInodeCaps[md_pino].end(); ++it) {
      shared_cap cap;

      // loop over all caps for that inode
      if (mCaps.count(*it)) {
        cap = mCaps[*it];
      } else {
        continue;
      }

      eos_static_info("id=%lx clientid=%s clientuuid=%s authid=%s",
                      cap->id(),
                      cap->clientid().c_str(),
                      cap->clientuuid().c_str(),
                      cap->authid().c_str());

      // skip our own cap!
      if (cap->authid() == md.authid()) {
        continue;
      }

      // skip identical client mounts, the have it anyway!
      if (cap->clientuuid() == refcap->clientuuid()) {
        continue;
      }

      // skip same source
      if (cap->clientuuid() == md.clientuuid()) {
        continue;
      }

      if (cap->id() && !clients_sent.count(cap->clientuuid())) {
        bccaps.push_back(cap);
        // make sure we sent the update only once to each client, eveh if this
        // one has many caps
        clients_sent.insert(cap->clientuuid());
      }
    }
  }

  lLock.Release();

  for (auto it : bccaps) {
    gOFS->zMQ->gFuseServer.Client().SendMD(md,
                                           it->clientuuid(),
                                           it->clientid(),
                                           md_ino,
                                           md_pino,
                                           clock,
                                           p_mtime);
    errno = 0;
  }

  EXEC_TIMING_END("Eosxd::int::BcMD");
  return 0;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
std::string
FuseServer::Caps::Print(std::string option, std::string filter)
{
  std::string out;
  std::string astring;
  uint64_t now = (uint64_t) time(NULL);
  eos::common::RWMutexReadLock lock;

  if (option == "p") {
    lock.Grab(gOFS->eosViewRWMutex);
  }

  eos::common::RWMutexReadLock lLock(*this);
  eos_static_info("option=%s string=%s", option.c_str(), filter.c_str());
  regex_t regex;

  if (filter.size() &&
      regcomp(&regex, filter.c_str(), REG_ICASE | REG_EXTENDED | REG_NOSUB)) {
    out = "error: illegal regular expression ;";
    out += filter.c_str();
    out += "'\n";
    return out;
  }

  if (option == "t") {
    // print by time order
    for (auto it = mTimeOrderedCap.begin(); it != mTimeOrderedCap.end();) {
      if (!mCaps.count(it->second)) {
        it = mTimeOrderedCap.erase(it);
        continue;
      }

      char ahex[256];
      shared_cap cap = mCaps[it->second];
      snprintf(ahex, sizeof(ahex), "%016lx", (unsigned long) cap->id());
      std::string match = "";
      match += "# i:";
      match += ahex;
      match += " a:";
      match += cap->authid();
      match += " c:";
      match += cap->clientid();
      match += " u:";
      match += cap->clientuuid();
      match += " m:";
      snprintf(ahex, sizeof(ahex), "%08lx", (unsigned long) cap->mode());
      match += ahex;
      match += " v:";

      if ((cap->vtime() - now) >  0) {
        match += eos::common::StringConversion::GetSizeString(astring,
                 (unsigned long long) cap->vtime() - now);
      } else {
        match += eos::common::StringConversion::GetSizeString(astring,
                 (unsigned long long) 0);
      }

      match += "\n";

      if (filter.size() &&
          (regexec(&regex, match.c_str(), 0, NULL, 0) == REG_NOMATCH)) {
        it++;
        continue;
      }

      out += match.c_str();
      ++it;
    }
  }

  if (option == "i") {
    // print by inode
    for (auto it = mInodeCaps.begin(); it != mInodeCaps.end(); ++it) {
      char ahex[256];
      snprintf(ahex, sizeof(ahex), "%016lx", (unsigned long) it->first);

      if (filter.size() && (regexec(&regex, ahex, 0, NULL, 0) == REG_NOMATCH)) {
        continue;
      }

      out += "# i:";
      out += ahex;
      out += "\n";

      for (auto sit = it->second.begin(); sit != it->second.end(); ++sit) {
        out += "___ a:";
        out += *sit;

        if (!mCaps.count(*sit)) {
          out += " c:<unfound> u:<unfound> m:<unfound> v:<unfound>\n";
        } else {
          shared_cap cap = mCaps[*sit];
          out += " c:";
          out += cap->clientid();
          out += " u:";
          out += cap->clientuuid();
          out += " m:";
          snprintf(ahex, sizeof(ahex), "%016lx", (unsigned long) cap->mode());
          out += ahex;
          out += " v:";
          out += eos::common::StringConversion::GetSizeString(astring,
                 (unsigned long long) cap->vtime() - now);
          out += "\n";
        }
      }
    }
  }

  if (option == "p") {
    // print by inode
    for (auto it = mInodeCaps.begin(); it != mInodeCaps.end(); ++it) {
      std::string spath;

      try {
        if (eos::common::FileId::IsFileInode(it->first)) {
          std::shared_ptr<eos::IFileMD> fmd =
            gOFS->eosFileService->getFileMD(eos::common::FileId::InodeToFid(it->first));
          spath = "f:";
          spath += gOFS->eosView->getUri(fmd.get());
        } else {
          std::shared_ptr<eos::IContainerMD> cmd =
            gOFS->eosDirectoryService->getContainerMD(it->first);
          spath = "d:";
          spath += gOFS->eosView->getUri(cmd.get());
        }
      } catch (eos::MDException& e) {
        spath = "<unknown>";
      }

      if (filter.size() &&
          (regexec(&regex, spath.c_str(), 0, NULL, 0) == REG_NOMATCH)) {
        continue;
      }

      char apath[1024];
      out += "# ";
      snprintf(apath, sizeof(apath), "%-80s", spath.c_str());
      out += apath;
      out += "\n";

      for (auto sit = it->second.begin(); sit != it->second.end(); ++sit) {
        out += "___ a:";
        out += *sit;

        if (!mCaps.count(*sit)) {
          out += " c:<unfound> u:<unfound> m:<unfound> v:<unfound>\n";
        } else {
          shared_cap cap = mCaps[*sit];
          out += " c:";
          out += cap->clientid();
          out += " u:";
          out += cap->clientuuid();
          out += " m:";
          char ahex[20];
          snprintf(ahex, sizeof(ahex), "%016lx", (unsigned long) cap->mode());
          out += ahex;
          out += " v:";
          out += eos::common::StringConversion::GetSizeString(astring,
                 (unsigned long long) cap->vtime() - now);
          out += "\n";
        }
      }
    }
  }

  return out;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
int
FuseServer::Caps::Delete(uint64_t md_ino)
{
  eos::common::RWMutexWriteLock lLock(*this);

  if (!mInodeCaps.count(md_ino)) {
    return ENOENT;
  }

  for (auto sit = mInodeCaps[md_ino].begin() ;
       sit != mInodeCaps[md_ino].end();
       ++sit) {
    for (auto it = mClientCaps.begin(); it != mClientCaps.end(); it++) {
      // erase authid from the client set
      it->second.erase(*sit);
    }

    if (mCaps.count(*sit)) {
      shared_cap cap = mCaps[*sit];

      if (mClientInoCaps.count(cap->clientid())) {
        mClientInoCaps[cap->clientid()].erase(md_ino);
      }

      mCaps.erase(*sit);
    }
  }

  // erase inode from the inode caps
  mInodeCaps.erase(md_ino);
  return 0;
}

EOSMGMNAMESPACE_END
