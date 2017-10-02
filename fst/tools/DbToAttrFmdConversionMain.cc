//------------------------------------------------------------------------------
//! @file DbToAttrFmdConversionMain.cc
//! @author Jozsef Makai<jmakai@cern.ch>
//------------------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2017 CERN/Switzerland                                  *
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

#include "fst/FmdDbMap.hh"
#include "fst/FmdAttributeHandler.hh"

#include <future>

using namespace eos;
using namespace eos::fst;

int main(int argc, char* argv[]) {
  if(argc < 3) {
    cerr << "Usage: eos-fst-fmd-dbattr-convert <md dictionary path> <db directory>" << endl;
    return -1;
  }

  const auto dictPath = argv[1];
  const auto dbPath = argv[2];

  eos::common::ZStandard fmdCompressor;
  fmdCompressor.SetDicts(dictPath);
  FmdAttributeHandler fmdAttributeHandler{&fmdCompressor, &gFmdClient};

  std::vector<std::future<void>> futures;
  for(const auto& fsid : FmdDbMapHandler::GetFsidInMetaDir(dbPath)) {
    auto future = std::async(
      std::launch::async,
      [&fmdAttributeHandler, &dbPath, fsid]() {
        XrdOucString dbfilename;
        FmdDbMapHandler fmdDbMapHandler;
        fmdDbMapHandler.CreateDBFileName(dbPath, dbfilename);
        fmdDbMapHandler.SetDBFile(dbfilename.c_str(), fsid);
        for(const auto& fmd : fmdDbMapHandler.RetrieveAllFmd()) {
          fmdAttributeHandler.FmdAttrSet(fmd, fmd.fid(), fmd.fsid(), nullptr);
        }
      }
    );
    futures.emplace_back(std::move(future));
  }

  for(auto&& future : futures) {
    future.get();
  }

  return 0;
}