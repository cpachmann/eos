// ----------------------------------------------------------------------
// File: BoundIdentityProvider.hh
// Author: Georgios Bitzes - CERN
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

#ifndef __BOUND_IDENTITY_PROVIDER__HH__
#define __BOUND_IDENTITY_PROVIDER__HH__

#include "CredentialValidator.hh"
#include "CredentialCache.hh"
#include "CredentialFinder.hh"
#include "ProcessInfo.hh"
#include "EnvironmentReader.hh"
#include "SecurityChecker.hh"
#include "JailedPath.hh"
#include <XrdSec/XrdSecEntity.hh>
#include <XrdSecsss/XrdSecsssID.hh>
#include <atomic>

class BoundIdentityProvider
{
public:
  BoundIdentityProvider();

  CredentialState
  fillCredsFromEnv(const Environment& env, const CredentialConfig& credConfig,
                   UserCredentials& creds, uid_t uid, gid_t gid);

  CredentialState
  retrieve(const Environment& env, uid_t uid, gid_t gid, bool reconnect,
           std::shared_ptr<const BoundIdentity>& result);

  CredentialState
  retrieve(pid_t pid, uid_t uid, gid_t gid, bool reconnect,
           std::shared_ptr<const BoundIdentity>& result);

  CredentialState
  useDefaultPaths(uid_t uid, gid_t gid, bool reconnect,
                  std::shared_ptr<const BoundIdentity>& result);

  CredentialState
  useGlobalBinding(uid_t uid, gid_t gid, bool reconnect,
                   std::shared_ptr<const BoundIdentity>& result);

  std::shared_ptr<const BoundIdentity>
  retrieve(pid_t pid, uid_t uid, gid_t gid, bool reconnect);

  void setCredentialConfig(const CredentialConfig& conf)
  {
    credConfig = conf;
    // For some reason, doing this in the constructor results in crazy behavior,
    // like threads not waking up from the condition variable.
    // (or even std::this_thread::sleep_for !! ) Investigate?
    environmentReader.launchWorkers(3);
  }

  bool isStillValid(const BoundIdentity& identity);

  SecurityChecker& getSecurityChecker()
  {
    return securityChecker;
  }

  EnvironmentReader& getEnvironmentReader()
  {
    return environmentReader;
  }

  CredentialState unixAuthentication(uid_t uid, gid_t gid, pid_t pid,
                                     bool reconnect, std::shared_ptr<const BoundIdentity>& result);
private:
  SecurityChecker securityChecker;
  CredentialValidator validator;
  CredentialConfig credConfig;
  CredentialCache credentialCache;
  EnvironmentReader environmentReader;
  XrdSecsssID* sssRegistry;

  CredentialState fillKrb5FromEnv(const Environment& env, UserCredentials& creds,
                                  uid_t uid, gid_t gid);
  CredentialState fillX509FromEnv(const Environment& env, UserCredentials& creds,
                                  uid_t uid, gid_t gid);

  CredentialState fillSssFromEnv(const Environment& env, UserCredentials& creds,
                                 uid_t uid, gid_t gid);

  uint64_t getUnixConnectionCounter(uid_t uid, gid_t gid, bool reconnect);

  //----------------------------------------------------------------------------
  // Given a set of user-provided, non-trusted UserCredentials, attempt to
  // translate them into a BoundIdentity object. (either by allocating a new
  // connection, or re-using a cached one)
  //
  // If such a thing is not possible, return false.
  //----------------------------------------------------------------------------
  bool userCredsToBoundIdentity(UserCredentials &creds,
    std::shared_ptr<const BoundIdentity>& result, bool reconnect);

  //----------------------------------------------------------------------------
  // Register SSS credentials
  //----------------------------------------------------------------------------
  void registerSSS(const BoundIdentity& bdi);

  std::mutex unixConnectionCounterMtx;
  std::map<std::pair<uid_t, gid_t>, uint64_t> unixConnectionCounter;

  std::atomic<uint64_t> connectionCounter{1};
};

#endif
