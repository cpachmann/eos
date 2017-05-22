// ----------------------------------------------------------------------
// File: Utils.cc
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

#include "Utils.hh"

bool readFile(const std::string &path, std::string &contents) {
  bool retvalue = true;
  std::ostringstream ss;

  const int BUFFER_SIZE = 1024;
  char buffer[BUFFER_SIZE];

  FILE *in = fopen(path.c_str(), "rb");
  if(!in) {
    return false;
  }

  while(true) {
    size_t bytesRead = fread(buffer, 1, BUFFER_SIZE, in);

    if(bytesRead > 0) {
      ss.write(buffer, bytesRead);
    }

    if(bytesRead < 0) {
      retvalue = false;
      break;
    }

    if(bytesRead != BUFFER_SIZE) {
      break;
    }
  }

  fclose(in);
  contents = ss.str();
  return retvalue;
}
