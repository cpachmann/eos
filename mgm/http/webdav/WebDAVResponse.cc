// ----------------------------------------------------------------------
// File: WebDAVResponse.cc
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
#include "mgm/http/webdav/WebDAVResponse.hh"
#include "mgm/Namespace.hh"
#include "common/Logging.hh"
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

EOSMGMNAMESPACE_BEGIN

/*----------------------------------------------------------------------------*/
WebDAVResponse::WebDAVResponse (eos::common::HttpRequest *request)
{
  using namespace rapidxml;

  std::string body;
  // Make a safe-to-modify copy of the request XML

  if (request->GetBody().length()) 
  {
    body = request->GetBody();
  }
  else
  {
    // empty body is an allprop request
    body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<propfind xmlns=\"DAV:\"><allprop/></propfind>";
  }

  
  mXMLRequestCopy = std::vector<char>(body.begin(),
				      body.end());
  mXMLRequestCopy.push_back('\0');

  // Parse the request
  try
  {
    mXMLRequestDocument.parse<0>(&mXMLRequestCopy[0]);
  }
  catch (const rapidxml::parse_error &e)
  {
    eos_static_err("msg=\"error parsing XML document: %s\"", e.what());
  }
};

/*----------------------------------------------------------------------------*/
void
WebDAVResponse::ParseNamespaces ()
{
  using namespace rapidxml;
  xml_node<> *node = mXMLRequestDocument.first_node();

  while (node)
  {
    xml_attribute<> *attribute = node->first_attribute();
    while (attribute)
    {
      XrdOucString attributeName(attribute->name());
      std::string attributeValue(attribute->value());

      if (attributeName.beginswith("xmlns"))
      {
        std::string ns;

        // split off the xmlns:<ns> (if any)
        int colon = 0;
        if ((colon = attributeName.find(':')) != STR_NPOS)
          ns = std::string(std::string(attributeName.c_str()), colon + 1);

        eos_static_debug("namespace=\"%s\"", ns != "" ? ns.c_str() : "default");

        if (attributeValue == "DAV:")
          mDAVNamespaces[ns] = attributeValue;
        else
          mCustomNamespaces[ns] = attributeValue;
      }

      attribute = attribute->next_attribute();
    }

    node = node->next_sibling();
  }
}

/*----------------------------------------------------------------------------*/
rapidxml::xml_node<>*
WebDAVResponse::GetNode (rapidxml::xml_node<> *node, const char *name)
{
  XrdOucString target(name);
  rapidxml::xml_node<> *child = node->first_node();

  while (child)
  {
    for (auto it = mDAVNamespaces.begin(); it != mDAVNamespaces.end(); ++it)
    {
      std::string full(it->first);
      if (full.length()) {
	// empty namespaces do not requre a ':'
	full += ":";
      }
      full += name;
      eos_static_debug("namespace(dav)=\"%s\" child=\"%s\"", full.c_str(), child->name());
      if (std::string(child->name()) == full)
        return child;
    }


    for (auto it = mCustomNamespaces.begin(); it != mCustomNamespaces.end(); ++it)
    {
      std::string full(it->first);
      if (full.length()) {
	// empty namespaces do not requre a ':'
	full += ":";
      }
      full += name;
      eos_static_debug("namespace(custom)=\"%s\" child=\"%s\"", full.c_str(), child->name());
      if (std::string(child->name()) == full)
        return child;
    }

    child = child->next_sibling();
  }
  return NULL;
}

/*----------------------------------------------------------------------------*/
rapidxml::xml_node<>*
WebDAVResponse::AllocateNode (const char *name)
{
  return mXMLResponseDocument.allocate_node(rapidxml::node_element,
                                            AllocateString(name));
}

/*----------------------------------------------------------------------------*/
rapidxml::xml_attribute<>*
WebDAVResponse::AllocateAttribute (const char *name, const char *value)
{
  return mXMLResponseDocument.allocate_attribute(AllocateString(name),
                                                 AllocateString(value));
}

/*----------------------------------------------------------------------------*/
rapidxml::xml_node<>*
WebDAVResponse::CloneNode (rapidxml::xml_node<> *node)
{
  return mXMLResponseDocument.clone_node(node);
}

/*----------------------------------------------------------------------------*/
const char*
WebDAVResponse::AllocateString (const char *value)
{
  return mXMLResponseDocument.allocate_string(value);
}

/*----------------------------------------------------------------------------*/
void
WebDAVResponse::SetValue(rapidxml::xml_node<> *node, const char *value)
{
  node->value(AllocateString(value));
}

/*----------------------------------------------------------------------------*/
EOSMGMNAMESPACE_END
