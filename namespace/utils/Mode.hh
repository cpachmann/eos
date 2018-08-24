/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2018 CERN/Switzerland                                  *
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

//------------------------------------------------------------------------------
// author: Georgios Bitzes <georgios.bitzes@cern.ch>
// desc:   Namespace mode utilities
//------------------------------------------------------------------------------

#ifndef EOS_NS_MODE_HH
#define EOS_NS_MODE_HH

#include "common/Logging.hh"

namespace eos
{

  //------------------------------------------------------------------------------
  //! Convert mode to its corresponding character in a listing.
  //! eg S_IFDIR is converted to 'd'
  //------------------------------------------------------------------------------
  inline char modeToFileTypeChar(mode_t mode) {
    unsigned int filetype = S_IFMT & mode;

    switch (filetype) {
      case S_IFIFO:  return 'p';
      case S_IFCHR:  return 'c';
      case S_IFDIR:  return 'd';
      case S_IFBLK:  return 'b';
      case S_IFREG:  return '-';
      case S_IFLNK:  return 'l';
      case S_IFSOCK: return 's';
      default: {
        // TODO(gbitzes): Eventually convert this to an assertion?
        eos_static_crit(
          "Unable to translate mode to filetype char. mode=%d, filetype=%d",
          mode, filetype);
        return '-';
      }
    }
  }

}

#endif