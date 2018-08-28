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
// desc:   Namespace checksum utilities
//------------------------------------------------------------------------------

#ifndef EOS_NS_CHECKSUM_HH
#define EOS_NS_CHECKSUM_HH

#include "common/Logging.hh"
#include "common/LayoutId.hh"

namespace eos
{
  //----------------------------------------------------------------------------
  //! Append FileMD checksum onto the given string. Return false only if we're
  //! not able to determine checksum type for given layout id.
  //!
  //! Supply space == true to separate each two hexademical digits with a space.
  //! ie "b5 e1 70 20", instead of "b5e17020"
  //!
  //! We use a template to support both std::string, and XrdOucString...
  //----------------------------------------------------------------------------
  template<typename StringType>
  bool appendChecksumOnStringAsHex(const eos::IFileMD *fmd, StringType &out, bool space=false) {
    if(!fmd) return false;

    unsigned int nominalChecksumLength = eos::common::LayoutId::GetChecksumLen(fmd->getLayoutId());
    Buffer buffer = fmd->getChecksum();

    for(unsigned int i = 0; i < nominalChecksumLength; i++) {
      char hb[4];

      if(space && i != (nominalChecksumLength-1)) {
        sprintf(hb, "%02x ", (unsigned char)(fmd->getChecksum().getDataPadded(i)));
        out += hb;
      }
      else {
        sprintf(hb, "%02x", (unsigned char)(fmd->getChecksum().getDataPadded(i)));
        out += hb;
      }
    }

    return (nominalChecksumLength > 0);
  }

}

#endif