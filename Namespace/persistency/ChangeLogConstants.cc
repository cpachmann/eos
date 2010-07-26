//------------------------------------------------------------------------------
// author: Lukasz Janyst <ljanyst@cern.ch>
// desc:   Some constants concerning the change log data
//------------------------------------------------------------------------------

#include "Namespace/persistency/ChangeLogConstants.hh"

namespace eos
{
  const uint8_t  UPDATE_RECORD_MAGIC  = 1;
  const uint8_t  DELETE_RECORD_MAGIC  = 2;
  const uint16_t FILE_LOG_MAGIC       = 1;
  const uint16_t CONTAINER_LOG_MAGIC  = 2;
}
