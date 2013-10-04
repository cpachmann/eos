// ----------------------------------------------------------------------
// File: HttpHandler.cc
// Author: Justin Lewis Salmon - CERN
// ----------------------------------------------------------------------

/************************************************************************
 * EOS - the CERN Disk Storage System                                   *
 * Copyright (C) 2013 CERN/Switzerland                                  *
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

/*----------------------------------------------------------------------------*/
#include "mgm/http/HttpHandler.hh"
#include "mgm/XrdMgmOfsDirectory.hh"
#include "mgm/Namespace.hh"
#include "mgm/XrdMgmOfs.hh"
#include "common/http/PlainHttpResponse.hh"
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/

EOSMGMNAMESPACE_BEGIN

/*----------------------------------------------------------------------------*/
bool
HttpHandler::Matches (const std::string &meth, HeaderMap &headers)
{
  int method = ParseMethodString(meth);
  if (method == GET || method == HEAD || method == POST ||
      method == PUT || method == DELETE || method == TRACE ||
      method == OPTIONS || method == CONNECT || method == PATCH)
  {
    eos_static_info("Matched HTTP protocol for request");
    return true;
  }
  else return false;
}

/*----------------------------------------------------------------------------*/
void
HttpHandler::HandleRequest (eos::common::HttpRequest *request)
{
  eos_static_info("handling http request");
  eos::common::HttpResponse *response = 0;

  int meth = ParseMethodString(request->GetMethod());
  switch (meth)
  {
  case GET:
    response = Get(request);
    break;
  case HEAD:
    response = Head(request);
    break;
  case POST:
    response = Post(request);
    break;
  case PUT:
    response = Put(request);
    break;
  case DELETE:
    response = Delete(request);
    break;
  case TRACE:
    response = Trace(request);
    break;
  case OPTIONS:
    response = Options(request);
    break;
  case CONNECT:
    response = Connect(request);
    break;
  case PATCH:
    response = Patch(request);
    break;
  default:
    response = new eos::common::PlainHttpResponse();
    response->SetResponseCode(eos::common::HttpResponse::BAD_REQUEST);
    response->SetBody("No such method");
  }

  mHttpResponse = response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse*
HttpHandler::Get (eos::common::HttpRequest *request)
{
  XrdSecEntity client(mVirtualIdentity->prot.c_str());
  client.name = const_cast<char*> (mVirtualIdentity->name.c_str());
  client.host = const_cast<char*> (mVirtualIdentity->host.c_str());
  client.tident = const_cast<char*> (mVirtualIdentity->tident.c_str());

  // Classify path to split between directory or file objects
  bool isfile = true;
  std::string url = request->GetUrl();
  std::string query = request->GetQuery();
  eos::common::HttpResponse *response = 0;

  XrdOucString spath = request->GetUrl().c_str();
  if (!spath.beginswith("/proc/"))
  {
    if (spath.endswith("/"))
    {
      isfile = false;
    }
    else
    {
      struct stat buf;
      XrdOucErrInfo error;
      // find out if it is a file or directory
      if (gOFS->stat(url.c_str(), &buf, error, &client, ""))
      {
        response = HttpServer::HttpError("Not such file or directory",
                                         response->NOT_FOUND);
        return response;
      }
      if (S_ISDIR(buf.st_mode))
        isfile = false;
    }
  }

  if (isfile)
  {
    XrdSfsFile* file = gOFS->newFile(client.name);
    if (file)
    {
      XrdSfsFileOpenMode open_mode = 0;
      mode_t create_mode = 0;

      int rc = file->open(url.c_str(), open_mode, create_mode, &client,
                          query.c_str());
      if ((rc != SFS_REDIRECT) && open_mode)
      {
        // retry as a file creation
        open_mode |= SFS_O_CREAT;
        rc = file->open(url.c_str(), open_mode, create_mode, &client,
                        query.c_str());
      }

      if (rc != SFS_OK)
      {
        if (rc == SFS_REDIRECT)
        {
          // the embedded server on FSTs is hardcoded to run on port 8001
          response = HttpServer::HttpRedirect(request->GetUrl(),
                                              file->error.getErrText(),
                                              8001, false);
        }
        else
          if (rc == SFS_ERROR)
        {
          if (file->error.getErrInfo() == ENODEV)
          {
            response = new eos::common::PlainHttpResponse();
          }
          else
          {
            response = HttpServer::HttpError(file->error.getErrText(),
                                             file->error.getErrInfo());
          }
        }
        else
          if (rc == SFS_DATA)
        {
          response = HttpServer::HttpData(file->error.getErrText(),
                                          file->error.getErrInfo());
        }
        else
          if (rc == SFS_STALL)
        {
          response = HttpServer::HttpStall(file->error.getErrText(),
                                           file->error.getErrInfo());
        }
        else
        {
          response = HttpServer::HttpError("Unexpected result from file open",
                                           EOPNOTSUPP);
        }
      }
      else
      {
        char buffer[65536];
        offset_t offset = 0;
        std::string result;
        do
        {
          size_t nread = file->read(offset, buffer, sizeof (buffer));
          if (nread > 0)
          {
            result.append(buffer, nread);
          }
          if (nread != sizeof (buffer))
          {
            break;
          }
        }
        while (1);
        file->close();
        response = new eos::common::PlainHttpResponse();
        response->SetBody(result);
      }
      // clean up the object
      delete file;
    }
  }
  else
  {
    XrdMgmOfsDirectory directory;
    int listrc = directory.open(request->GetUrl().c_str(), *mVirtualIdentity,
                                (const char*) 0);

    if (!listrc)
    {
      std::string result;
      const char *val;
      result += "<!DOCTYPE html>\n";
      //result += "<head>\n<style type=\"text/css\">\n<!--\nbody "
      //  "{font-family:Arial, sans-serif; font-weight:lighter}\n-->\n</style>\n</head>";
      result += "<head>\n"
        " <title>EOS HTTP Browser</title>"
        "<link rel=\"stylesheet\" href=\"http://www.w3.org/StyleSheets/Core/Chocolate\" "
        "</head>\n";

      result += "<html>\n";
      result += "<body>\n";
      result += "<img src=\"http://eos.cern.ch/images/EOS-Browsing.jpg\" "
        "alt=\"EOS Browser\" width=\"1000\" height=\"120\" style=\"border: #00008B 0px solid;\""
        ">\n";
      result += "<hr style=\"border:solid #00ffff 3px;background-color:#0000ff;"
        "height:10px;width:400px;text-align:left;\">";
      result += "<h2> <font color=\"#2C3539\">[ ";
      result += gOFS-> MgmOfsInstanceName.c_str();
      result += " ]:</font> ";
      result += url.c_str();
      result += "</h2>";
      result += "<div><table>\n";
      while ((val = directory.nextEntry()))
      {
        XrdOucString entryname = val;
        XrdOucString linkname = "";

        if ((spath == "/") &&
            ((entryname == ".") ||
             (entryname == "..")))
          continue;

        result += "<tr>\n";
        result += "  <td>";
        result += "<a href=\"";
        if (entryname == ".")
        {
          linkname = spath.c_str();
        }
        else
        {
          if (entryname == "..")
          {
            if (spath != "/")
            {
              eos::common::Path cPath(spath.c_str());
              linkname = cPath.GetParentPath();
            }
            else
            {
              linkname = "/";
            }
          }
          else
          {
            linkname = spath.c_str();
            if (!spath.endswith("/") && (spath != "/"))
              linkname += "/";
            linkname += entryname.c_str();

          }
        }
        result += linkname.c_str();
        result += "\">";
        result += "<font size=\"2\">";
        result += entryname.c_str();
        result += "</font>";
        struct stat buf;
        buf.st_mode = 0;
        XrdOucErrInfo error;
        XrdOucString sizestring;
        XrdOucString entrypath = spath.c_str();
        entrypath += "/";
        entrypath += entryname.c_str();
        fprintf(stderr, "Stat %s\n", entrypath.c_str());
        // find out if it is a file or directory
        if (!gOFS->stat(entrypath.c_str(), &buf, error, &client, ""))
          if (S_ISDIR(buf.st_mode))
            result += "/";

        result += "  </td>\n";
        result += "  <td>";
        result += "<font size=\"2\">";
        if (S_ISDIR(buf.st_mode))
          result += "";
        else
          result += eos::common::StringConversion::GetReadableSizeString(sizestring, buf.st_size, "Bytes");
        result += "</font>";
        result += "</td>\n";
        result += "</tr>\n";
      }
      result += "</table></div>\n";
      result += "</body>\n";
      result += "</html>\n";
      response = new eos::common::PlainHttpResponse();
      response->SetBody(result);
    }
    else
    {
      response = HttpServer::HttpError("Unable to open directory",
                                       errno);
    }
  }

  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Head (eos::common::HttpRequest * request)
{
  eos_static_info("Calling HEAD ");
  eos::common::HttpResponse *response = Get(request);
  response->SetBody(std::string(""));
  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Post (eos::common::HttpRequest * request)
{
  using namespace eos::common;
  HttpResponse *response = new PlainHttpResponse();
  response->SetResponseCode(HttpResponse::ResponseCodes::NOT_IMPLEMENTED);
  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Put (eos::common::HttpRequest * request)
{
  XrdSecEntity client(mVirtualIdentity->prot.c_str());
  client.name = const_cast<char*> (mVirtualIdentity->name.c_str());
  client.host = const_cast<char*> (mVirtualIdentity->host.c_str());
  client.tident = const_cast<char*> (mVirtualIdentity->tident.c_str());

  // Classify path to split between directory or file objects
  bool isfile = true;
  std::string url = request->GetUrl();
  eos::common::HttpResponse *response = 0;

  XrdOucString spath = request->GetUrl().c_str();
  if (!spath.beginswith("/proc/"))
  {
    if (spath.endswith("/"))
    {
      isfile = false;
    }
  }

  if (isfile)
  {
    XrdSfsFile* file = gOFS->newFile(client.name);
    if (file)
    {
      XrdSfsFileOpenMode open_mode = 0;
      mode_t create_mode = 0;

      // use the proper creation/open flags for PUT's
      open_mode |= SFS_O_TRUNC;
      open_mode |= SFS_O_RDWR;
      open_mode |= SFS_O_MKPTH;
      create_mode |= (SFS_O_MKPTH | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

      std::string query;
      if (request->GetHeaders()["Content-Length"] == "0" ||
          *request->GetBodySize() == 0)
      {
        query += "eos.bookingsize=0";
      }

      int rc = file->open(url.c_str(), open_mode, create_mode, &client,
                          query.c_str());
      if (rc != SFS_OK)
      {
        if ((rc != SFS_REDIRECT) && open_mode)
        {
          // retry as a file creation
          open_mode |= SFS_O_CREAT;
          rc = file->open(url.c_str(), open_mode, create_mode, &client,
                          query.c_str());
        }
      }

      if (rc != SFS_OK)
      {
        if (rc == SFS_REDIRECT)
        {
          // the embedded server on FSTs is hardcoded to run on port 8001
          response = HttpServer::HttpRedirect(request->GetUrl(),
                                              file->error.getErrText(),
                                              8001, false);
        }
        else if (rc == SFS_ERROR)
        {
          response = HttpServer::HttpError(file->error.getErrText(),
                                           file->error.getErrInfo());
        }
        else if (rc == SFS_DATA)
        {
          response = HttpServer::HttpData(file->error.getErrText(),
                                          file->error.getErrInfo());
        }
        else if (rc == SFS_STALL)
        {
          response = HttpServer::HttpStall(file->error.getErrText(),
                                           file->error.getErrInfo());
        }
        else
        {
          response = HttpServer::HttpError("Unexpected result from file open",
                                           EOPNOTSUPP);
        }
      }
      else
      {
        response = new eos::common::PlainHttpResponse();
        response->SetResponseCode(response->CREATED);
      }
      // clean up the object
      delete file;
    }
  }
  else
  {
    // DIR requests
    response = HttpServer::HttpError("Not Implemented", EOPNOTSUPP);
  }

  return response;

}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Delete (eos::common::HttpRequest * request)
{
  eos::common::HttpResponse *response = 0;
  XrdOucErrInfo error;
  struct stat buf;
  ProcCommand cmd;

  gOFS->_stat(request->GetUrl().c_str(), &buf, error, *mVirtualIdentity, "");

  XrdOucString info = "mgm.cmd=rm&mgm.path=";
  info += request->GetUrl().c_str();
  if (S_ISDIR(buf.st_mode)) info += "&mgm.option=r";

  cmd.open("/proc/user", info.c_str(), *mVirtualIdentity, &error);
  cmd.close();
  int rc = cmd.GetRetc();

  if (rc != SFS_OK)
  {
    if (error.getErrInfo() == EPERM)
    {
      response = HttpServer::HttpError(error.getErrText(), response->FORBIDDEN);
    }
    else if (error.getErrInfo() == ENOENT)
    {
      response = HttpServer::HttpError(error.getErrText(), response->NOT_FOUND);
    }
    else
    {
      response = HttpServer::HttpError(error.getErrText(), error.getErrInfo());
    }
  }
  else
  {
    response = new eos::common::PlainHttpResponse();
    response->SetResponseCode(response->NO_CONTENT);
  }

  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Trace (eos::common::HttpRequest * request)
{
  using namespace eos::common;
  HttpResponse *response = new PlainHttpResponse();
  response->SetResponseCode(HttpResponse::ResponseCodes::NOT_IMPLEMENTED);
  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Options (eos::common::HttpRequest * request)
{
  eos::common::HttpResponse *response = new eos::common::PlainHttpResponse();
  response->AddHeader("DAV", "1,2");
  response->AddHeader("Allow", "OPTIONS,GET,HEAD,POST,DELETE,TRACE,"\
                               "PROPFIND,PROPPATCH,COPY,MOVE,LOCK,UNLOCK");
  response->AddHeader("Content-Length", "0");

  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Connect (eos::common::HttpRequest * request)
{
  using namespace eos::common;
  HttpResponse *response = new PlainHttpResponse();
  response->SetResponseCode(HttpResponse::ResponseCodes::NOT_IMPLEMENTED);
  return response;
}

/*----------------------------------------------------------------------------*/
eos::common::HttpResponse *
HttpHandler::Patch (eos::common::HttpRequest * request)
{
  using namespace eos::common;
  HttpResponse *response = new PlainHttpResponse();
  response->SetResponseCode(HttpResponse::ResponseCodes::NOT_IMPLEMENTED);
  return response;
}

/*----------------------------------------------------------------------------*/
EOSMGMNAMESPACE_END
