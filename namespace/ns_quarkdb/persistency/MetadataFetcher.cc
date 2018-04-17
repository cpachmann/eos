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
//! @author Georgios Bitzes <georgios.bitzes@cern.ch>
//! @brief Class to retrieve metadata from the backend - no caching!
//------------------------------------------------------------------------------

#include <functional>
#include "namespace/ns_quarkdb/persistency/MetadataFetcher.hh"
#include "namespace/ns_quarkdb/persistency/ContainerMDSvc.hh"
#include "namespace/ns_quarkdb/persistency/FileMDSvc.hh"
#include "namespace/ns_quarkdb/persistency/Serialization.hh"
#include "qclient/QClient.hh"

#define SSTR(message) static_cast<std::ostringstream&>(std::ostringstream().flush() << message).str()
#define DBG(message) std::cerr << __FILE__ << ":" << __LINE__ << " -- " \
  << #message << " = " << message << std::endl

using redisReplyPtr = qclient::redisReplyPtr;
using std::placeholders::_1;

EOSNSNAMESPACE_BEGIN

//------------------------------------------------------------------------------
//! Helper method to check that the redis reply is a string reply
//!
//! @param reply redis reply
//------------------------------------------------------------------------------
MDStatus ensureStringReply(redisReplyPtr& reply)
{
  if (!reply) {
    return MDStatus(EFAULT, "QuarkDB backend not available!");
  }

  if ((reply->type == REDIS_REPLY_NIL) ||
      (reply->type == REDIS_REPLY_STRING && reply->len == 0)) {
    return MDStatus(ENOENT, "Empty response");
  }

  if (reply->type != REDIS_REPLY_STRING) {
    return MDStatus(EFAULT, SSTR("Received unexpected response, was expecting string: "
                                 << qclient::describeRedisReply(reply)));
  }

  return MDStatus();
}

//------------------------------------------------------------------------------
//! Class ContainerMdFetcher
//------------------------------------------------------------------------------
class ContainerMdFetcher : public qclient::QCallback
{
public:
  //----------------------------------------------------------------------------
  //! Constructor
  //----------------------------------------------------------------------------
  ContainerMdFetcher() = default;

  //----------------------------------------------------------------------------
  //! Initialize
  //!
  //! @param qcl qclient object
  //! @param id container id
  //!
  //! @return future holding the container metadata protobuf object
  //----------------------------------------------------------------------------
  std::future<eos::ns::ContainerMdProto>
  initialize(qclient::QClient& qcl, id_t id)
  {
    std::future<eos::ns::ContainerMdProto> fut = mPromise.get_future();
    mId = id;
    // There's a particularly evil race condition here: From the point we call
    // execCB and onwards, we must assume that the callback has arrived,
    // and *this has been destroyed already.
    // Not safe to access member variables after execCB, so we fetch the future
    // beforehand.
    qcl.execCB(this, "HGET", ContainerMDSvc::getBucketKey(mId), SSTR(mId));
    // fut is a stack object, safe to access.
    return fut;
  }

  //----------------------------------------------------------------------------
  //! Handle response
  //!
  //! @param reply holds a redis reply object
  //----------------------------------------------------------------------------
  virtual void handleResponse(redisReplyPtr&& reply)
  {
    MDStatus status = ensureStringReply(reply);

    if (!status.ok()) {
      return set_exception(status);
    }

    eos::ns::ContainerMdProto proto;
    status = Serialization::deserialize(reply->str, reply->len, proto);

    if (!status.ok()) {
      return set_exception(status);
    }

    // If we've made it this far, it's a success
    return set_value(proto);
  }

private:
  //----------------------------------------------------------------------------
  //! Return the container metadata proto object by passing it to the promise
  //!
  //! @param proto container metadata proto object to return
  //----------------------------------------------------------------------------
  void set_value(const eos::ns::ContainerMdProto& proto)
  {
    mPromise.set_value(proto);
    delete this; // harakiri
  }

  //----------------------------------------------------------------------------
  //! Return exception by passing it to the promise
  //!
  //! @param status error return status
  //----------------------------------------------------------------------------
  void set_exception(const MDStatus& status)
  {
    mPromise.set_exception(
      make_mdexception(status.getErrno(), "Error while fetching ContainerMD #"
                       << mId << " protobuf from QDB: " << status.getError()));
    delete this; // harakiri
  }

  id_t mId;
  std::promise<eos::ns::ContainerMdProto> mPromise;
};

//------------------------------------------------------------------------------
//! Struct MapFetcherFileTrait
//------------------------------------------------------------------------------
struct MapFetcherFileTrait {
  static std::string getKey(id_t id)
  {
    return SSTR(id << constants::sMapFilesSuffix);
  }

  using ContainerType = IContainerMD::FileMap;
};

//------------------------------------------------------------------------------
//! Struct MapFetcherContainerTrait
//------------------------------------------------------------------------------
struct MapFetcherContainerTrait {
  static std::string getKey(id_t id)
  {
    return SSTR(id << constants::sMapDirsSuffix);
  }

  using ContainerType = IContainerMD::ContainerMap;
};


//------------------------------------------------------------------------------
//! Class MapFetcher - fetches maps (ContainerMap, FileMap) of a particular
//! container.
//------------------------------------------------------------------------------
template<typename Trait>
class MapFetcher : public qclient::QCallback
{
public:
  using ContainerType = typename Trait::ContainerType;
  static constexpr size_t kCount = 250000;
  //----------------------------------------------------------------------------
  //! Constructor
  //----------------------------------------------------------------------------
  MapFetcher() = default;

  //----------------------------------------------------------------------------
  //! Initialize
  //!
  //! @param qcl qclient object
  //! @param trg target container id
  //----------------------------------------------------------------------------
  std::future<ContainerType> initialize(qclient::QClient& qcl, id_t trg)
  {
    mQcl = &qcl;
    mTarget = trg;
    mContents.set_deleted_key("");
    mContents.set_empty_key("##_EMPTY_##");
    std::future<ContainerType> fut = mPromise.get_future();
    // There's a particularly evil race condition here: From the point we call
    // execCB and onwards, we must assume that the callback has arrived,
    // and *this has been destroyed already.
    // Not safe to access member variables after execCB, so we fetch the future
    // beforehand.
    mQcl->execCB(this, "HSCAN", Trait::getKey(mTarget), "0", "COUNT", SSTR(kCount));
    // fut is a stack object, safe to access.
    return fut;
  }

  //----------------------------------------------------------------------------
  //! Handle response
  //!
  //! @param reply holds a redis reply object
  //----------------------------------------------------------------------------
  virtual void handleResponse(redisReplyPtr&& reply) override
  {
    if (!reply) {
      return set_exception(EFAULT, "QuarkDB backend not available!");
    }

    if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 2 ||
        (reply->element[0]->type != REDIS_REPLY_STRING) ||
        (reply->element[1]->type != REDIS_REPLY_ARRAY) ||
        ((reply->element[1]->elements % 2) != 0)) {
      return set_exception(EFAULT, SSTR("Received unexpected response: "
                                        << qclient::describeRedisReply(reply)));
    }

    std::string cursor = std::string(reply->element[0]->str,
                                     reply->element[0]->len);

    for (size_t i = 0; i < reply->element[1]->elements; i += 2) {
      redisReply* element = reply->element[1]->element[i];

      if (element->type != REDIS_REPLY_STRING) {
        return set_exception(EFAULT, SSTR("Received unexpected response: "
                                          << qclient::describeRedisReply(reply)));
      }

      std::string filename = std::string(element->str, element->len);
      element = reply->element[1]->element[i + 1];

      if (element->type != REDIS_REPLY_STRING) {
        return set_exception(EFAULT, SSTR("Received unexpected response: "
                                          << qclient::describeRedisReply(reply)));
      }

      int64_t value;
      MDStatus st = Serialization::deserialize(element->str, element->len, value);

      if (!st.ok()) {
        return set_exception(st);
      }

      mContents[filename] = value;
    }

    // Fire off next request?
    if (cursor == "0") {
      mPromise.set_value(std::move(mContents));
      delete this;
      return;
    }

    mQcl->execCB(this, "HSCAN", Trait::getKey(mTarget), cursor, "COUNT",
                 SSTR(kCount));
  }

private:
  //----------------------------------------------------------------------------
  //! Return exception by passing it to the promise
  //!
  //! @param status error return status
  //----------------------------------------------------------------------------
  void set_exception(const MDStatus& status)
  {
    return set_exception(status.getErrno(), status.getError());
  }

  //----------------------------------------------------------------------------
  //! Return exception by passing it to the promise
  //!
  //! @param status error return status
  //----------------------------------------------------------------------------
  void set_exception(int err, const std::string& msg)
  {
    mPromise.set_exception(
      make_mdexception(err, SSTR("Error while fetching file/container map for "
                                 "container #" << mTarget << " from QDB: "
                                 << msg)));
    delete this; // harakiri
  }

  qclient::QClient* mQcl;
  id_t mTarget;
  ContainerType mContents;
  std::promise<ContainerType> mPromise;
};

//------------------------------------------------------------------------------
// Parse FileMDProto from a redis response, throw on error.
//------------------------------------------------------------------------------
static eos::ns::FileMdProto parseFileMdProtoResponse(redisReplyPtr reply, id_t id) {
  ensureStringReply(reply).throwIfNotOk(SSTR("Error while fetching FileMD #" << id << " protobuf from QDB: "));

  eos::ns::FileMdProto proto;
  Serialization::deserialize(reply->str, reply->len, proto)
  .throwIfNotOk(SSTR("Error while fetching FileMD #" << id << " protobuf from QDB: "));

  return std::move(proto);
}

//------------------------------------------------------------------------------
// Fetch file metadata info for current id
//------------------------------------------------------------------------------
folly::Future<eos::ns::FileMdProto>
MetadataFetcher::getFileFromId(qclient::QClient& qcl, id_t id)
{
  return qcl.follyExec("HGET", FileMDSvc::getBucketKey(id), SSTR(id))
    .then(std::bind(parseFileMdProtoResponse, _1, id));
}

//------------------------------------------------------------------------------
// Fetch container metadata info for current id
//------------------------------------------------------------------------------
std::future<eos::ns::ContainerMdProto>
MetadataFetcher::getContainerFromId(qclient::QClient& qcl, id_t id)
{
  ContainerMdFetcher* fetcher = new ContainerMdFetcher();
  return fetcher->initialize(qcl, id);
}

//------------------------------------------------------------------------------
// Class MetadataFetcher
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Construct hmap key of subcontainers in container
//------------------------------------------------------------------------------
std::string MetadataFetcher::keySubContainers(id_t id)
{
  return SSTR(id << constants::sMapDirsSuffix);
}

//------------------------------------------------------------------------------
// Construct hmap key of files in container
//------------------------------------------------------------------------------
std::string MetadataFetcher::keySubFiles(id_t id)
{
  return SSTR(id << constants::sMapFilesSuffix);
}

//------------------------------------------------------------------------------
// Fetch all files for current id
//------------------------------------------------------------------------------
std::future<IContainerMD::FileMap>
MetadataFetcher::getFilesInContainer(qclient::QClient& qcl, id_t container)
{
  MapFetcher<MapFetcherFileTrait>* fetcher = new
  MapFetcher<MapFetcherFileTrait>();
  return fetcher->initialize(qcl, container);
}

//------------------------------------------------------------------------------
// Fetch all subcontaniers for current id
//------------------------------------------------------------------------------
std::future<IContainerMD::ContainerMap>
MetadataFetcher::getSubContainers(qclient::QClient& qcl, id_t container)
{
  MapFetcher<MapFetcherContainerTrait>* fetcher =
    new MapFetcher<MapFetcherContainerTrait>();
  return fetcher->initialize(qcl, container);
}

//------------------------------------------------------------------------------
// Parse response when looking up a ContainerID / FileID from (parent id, name)
//------------------------------------------------------------------------------
static id_t parseIDFromNameResponse(redisReplyPtr reply,
  id_t parentID, const std::string &name) {

  std::string errorPrefix = SSTR("Error while fetching FileID / ContainerID out of (parent id, name) = "
    "(" << parentID << ", " << name << "): ");

  ensureStringReply(reply).throwIfNotOk(errorPrefix);

  int64_t retval;
  Serialization::deserialize(reply->str, reply->len, retval)
    .throwIfNotOk(errorPrefix);

  return retval;
}

//------------------------------------------------------------------------------
// Fetch a file id given its parent and its name
//------------------------------------------------------------------------------
folly::Future<id_t>
MetadataFetcher::getFileIDFromName(qclient::QClient& qcl, id_t parent_id,
                                   const std::string& name)
{
  return qcl.follyExec("HGET", SSTR(parent_id << constants::sMapFilesSuffix), name)
    .then(std::bind(parseIDFromNameResponse, _1, parent_id, name));
}

//------------------------------------------------------------------------------
// Fetch a container id given its parent and its name
//------------------------------------------------------------------------------
folly::Future<id_t>
MetadataFetcher::getContainerIDFromName(qclient::QClient& qcl, id_t parent_id,
                                        const std::string& name)
{
  return qcl.follyExec("HGET", SSTR(parent_id << constants::sMapDirsSuffix), name)
    .then(std::bind(parseIDFromNameResponse, _1, parent_id, name));
}

EOSNSNAMESPACE_END
