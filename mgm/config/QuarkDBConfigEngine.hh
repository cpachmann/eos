//------------------------------------------------------------------------------
// @file QuarkDBConfigEngine.hh
// @author Andrea Manzi - CERN
//------------------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2016 CERN/Switzerland                                  *
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

#ifndef __EOSMGM_QUARKDBCONFIGENGINE__HH__
#define __EOSMGM_QUARKDBCONFIGENGINE__HH__

#include "mgm/config/IConfigEngine.hh"
#include "namespace/ns_quarkdb/BackendClient.hh"
#include "namespace/ns_quarkdb/qclient/include/qclient/QHash.hh"
#include "namespace/ns_quarkdb/qclient/include/qclient/QSet.hh"
#include "namespace/ns_quarkdb/qclient/include/qclient/AsyncHandler.hh"
#include "namespace/ns_quarkdb/qclient/include/qclient/QClient.hh"
#include "namespace/ns_quarkdb/QdbContactDetails.hh"

EOSMGMNAMESPACE_BEGIN

//------------------------------------------------------------------------------
//! Class QuarkDBCfgEngineChangelog
//------------------------------------------------------------------------------
class QuarkDBCfgEngineChangelog : public ICfgEngineChangelog
{
public:
  //----------------------------------------------------------------------------
  //! Constructor
  //!
  //! @param client qclient client
  //----------------------------------------------------------------------------
  QuarkDBCfgEngineChangelog(qclient::QClient* client);

  //----------------------------------------------------------------------------
  //! Destructor
  //----------------------------------------------------------------------------
  virtual ~QuarkDBCfgEngineChangelog() = default;

  //----------------------------------------------------------------------------
  //! Add entry to the changelog
  //!
  //! @param key      entry action
  //! @param value    entry key
  //! @param comment  entry value
  //----------------------------------------------------------------------------
  void AddEntry(const std::string& action, const std::string& key,
                const std::string& value);

  //----------------------------------------------------------------------------
  //! Get tail of the changelog
  //!
  //! @param nlines number of lines to return
  //! @param tail string to hold the response
  //!
  //! @return true if successful, otherwise false
  //----------------------------------------------------------------------------
  bool Tail(unsigned int nlines, XrdOucString& tail);

private:
  const std::string kChangelogKey = "eos-config-changelog"; ///< Changelog key
  qclient::QClient& mQcl;
};


//------------------------------------------------------------------------------
//! Class QuarkDBConfigEngine
//------------------------------------------------------------------------------
class QuarkDBConfigEngine : public IConfigEngine
{
public:
  //----------------------------------------------------------------------------
  //! Constructor
  //!
  //! @param configdir
  //! @param quarkDBcluster
  //----------------------------------------------------------------------------
  QuarkDBConfigEngine(const QdbContactDetails& contactDetails);

  //----------------------------------------------------------------------------
  //! Destructor
  //----------------------------------------------------------------------------
  virtual ~QuarkDBConfigEngine() = default;

  //----------------------------------------------------------------------------
  //! Load a given configuratino file
  //!
  //! @param env environment holding info about the configuration to be loaded
  //! @param err string holding any errors
  //! @param apply_stall_redirect if true then skip applying stall and redirect
  //!        rules from the configuration
  //!
  //! @return true if loaded successfully, otherwise false
  //----------------------------------------------------------------------------
  bool LoadConfig(XrdOucEnv& env, XrdOucString& err,
                  bool apply_stall_redirect = false) override;

  //----------------------------------------------------------------------------
  //! Save configuration to specified destination
  //!
  //! @param env environment holding info about the destination where the
  //!        current configuration will be saved
  //! @param err string holding any errors
  //!
  //! @return true if saved successfully, otherwise false
  //----------------------------------------------------------------------------
  bool SaveConfig(XrdOucEnv& env, XrdOucString& err);

  //----------------------------------------------------------------------------
  //! List all configurations
  //!
  //! @param configlist string holding the list of all configurations
  //! @param showbackup if true then show also the backups
  //!
  //! @return true if listing successful, otherwise false
  //----------------------------------------------------------------------------
  bool ListConfigs(XrdOucString& configlist, bool showbackups = false);


  //----------------------------------------------------------------------------
  //! Do an autosave
  //----------------------------------------------------------------------------
  bool AutoSave();

  //----------------------------------------------------------------------------
  //! Set a configuration value
  //!
  //! @param prefix identifies the type of configuration parameter
  //! @param key key of the configuration to set
  //! @param val value of the configuration
  //! @param not_bcast mark if change comes from a broadcast or not
  //----------------------------------------------------------------------------
  void SetConfigValue(const char* prefix, const char* key, const char* val,
                      bool not_bcast = true);

  //----------------------------------------------------------------------------
  //! Delete a configuration value
  //!
  //! @param prefix identifies the type of configuration parameter
  //! @param key key of the configuration to delete
  //! @param not_bcast mark if change comes from a broadcast or not
  //----------------------------------------------------------------------------
  void DeleteConfigValue(const char* prefix, const char* key,
                         bool not_bcast = true);

  //----------------------------------------------------------------------------
  //         QuarkDB configuration specific functions
  //----------------------------------------------------------------------------

  //----------------------------------------------------------------------------
  //! Push a configuration to QuarkDB
  //!
  //! @param env
  //! @param err
  //!
  //! @return true if successful, otherwise false
  //----------------------------------------------------------------------------
  bool PushToQuarkDB(XrdOucEnv& env, XrdOucString& err);

  //----------------------------------------------------------------------------
  //! Load a configuration from QuarkDB
  //!
  //! @param hash
  //! @param err
  //!
  //! @return true if successful, otherwise false
  //----------------------------------------------------------------------------
  bool PullFromQuarkDB(qclient::QHash& hash, XrdOucString& err);

  //----------------------------------------------------------------------------
  //! Set configuration folder
  //!
  //! @param configdir path of the new configuration folder
  //----------------------------------------------------------------------------
  void
  SetConfigDir(const char* configdir)
  {
    // noop
    mConfigFile = "default";
  }

private:
  //----------------------------------------------------------------------------
  // Store configuration into given name
  //----------------------------------------------------------------------------
  void storeIntoQuarkDB(const std::string &name);

  QdbContactDetails mQdbContactDetails;
  qclient::QClient* mQcl;
  const std::string kConfigurationHashKeyPrefix = "eos-config";
  const std::string kConfigurationBackupHashKeyPrefix = "eos-config-backup";

  //----------------------------------------------------------------------------
  //! Form configuration hash key
  //----------------------------------------------------------------------------
  std::string formConfigHashKey(const std::string& name)
  {
    return SSTR(kConfigurationHashKeyPrefix << ":" << name);
  }

  //----------------------------------------------------------------------------
  //! Form backup configuration hash key
  //----------------------------------------------------------------------------
  std::string formBackupConfigHashKey(const std::string& name, time_t timestamp)
  {
    char buff[128];
    time_t now = time(NULL);
    strftime(buff, 127, "%Y%m%d%H%M%S", localtime(&now));
    return SSTR(kConfigurationBackupHashKeyPrefix << ":" << name << "-" << buff);
  }

  //----------------------------------------------------------------------------
  //! Get current timestamp
  //!
  //! @param out output string
  //----------------------------------------------------------------------------
  void getTimeStamp(XrdOucString& out);

  //----------------------------------------------------------------------------
  //! Filter configuration
  //!
  //! @param info
  //! @param out
  //! @param cfg_name
  //----------------------------------------------------------------------------
  void FilterConfig(PrintInfo& info, XrdOucString& out, const char* cfg_name);
};

EOSMGMNAMESPACE_END

#endif
