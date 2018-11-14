//------------------------------------------------------------------------------
// File: JailIdentifier.cc
// Author: Georgios Bitzes, CERN
//------------------------------------------------------------------------------

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

#include "JailIdentifier.hh"
#include "Utils.hh"
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
JailResolver::JailResolver() {
  myJail = resolveIdentifier(getpid());
}

//------------------------------------------------------------------------------
// Resolve a given pid_t
//------------------------------------------------------------------------------
JailIdentifier JailResolver::resolveIdentifier(pid_t pid)
{
  std::string path = SSTR("/proc/" << pid << "/root");

  struct stat buf;
  if(stat(path.c_str(), &buf) != 0) {
  	int myerrno = errno;
  	return JailIdentifier::MakeError(myerrno, SSTR("Could not resolve jail of " << pid << ": " << strerror(myerrno)));
  }

  return JailIdentifier::Make(buf.st_dev, buf.st_ino);
}

//------------------------------------------------------------------------------
// Describe this object
//------------------------------------------------------------------------------
std::string JailIdentifier::describe() const {
  if(!ok()) {
  	return SSTR("Jail resolution failed: errno=" << errc << ", " << error);
  }

  return SSTR("Jail identifier: " << st_dev << "," << st_ino);
}

//----------------------------------------------------------------------------
// Check if the object contains an error
//----------------------------------------------------------------------------
bool JailIdentifier::ok() const {
  return error.empty();
}

//------------------------------------------------------------------------------
// Equality operator
//------------------------------------------------------------------------------
bool JailIdentifier::operator==(const JailIdentifier &other) const {
  if(errc != other.errc) {
  	return false;
  }

  if(error != other.error) {
  	return false;
  }

  if(st_dev != other.st_dev) {
  	return false;
  }

  if(st_ino != other.st_ino) {
  	return false;
  }

  return true;
}

//------------------------------------------------------------------------------
// Resolve a given pid_t to JailInformation
//------------------------------------------------------------------------------
JailInformation JailResolver::resolve(pid_t pid) {
  JailIdentifier id = resolveIdentifier(pid);
  return { id, pid, (id == myJail) };
}
