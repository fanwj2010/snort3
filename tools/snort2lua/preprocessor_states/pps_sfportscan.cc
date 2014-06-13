/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
 * Copyright (C) 2002-2013 Sourcefire, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.  You may not use, modify or
 * distribute this program under any other version of the GNU General
 * Public License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
// portscan.cc author Josh Rosenbaum <jorosenba@cisco.com>

#include <sstream>
#include <vector>
#include <iomanip>

#include "conversion_state.h"
#include "converter.h"
#include "snort2lua_util.h"

namespace {

class PortScan : public ConversionState
{
public:
    PortScan(Converter* cv)  : ConversionState(cv) {};
    virtual ~PortScan() {};
    virtual bool convert(std::stringstream& data_stream);

private:
    bool parse_list(std::string table_name, std::stringstream& data_stream);
    bool parse_option(std::string table_name, std::stringstream& data_stream);
    bool add_portscan_global_option(std::string name, std::stringstream& data_stream);
    // a wrapper for parse_list.  adds an addition '[..]' around the string
    bool parse_ip_list(std::string table_name, std::stringstream& data_stream);
};

} // namespace


bool PortScan::parse_ip_list(std::string list_name, std::stringstream& data_stream)
{
    std::string prev;
    std::string elem;

    if(!(data_stream >> elem) || (elem != "{"))
        return false;

    if(!(data_stream >> elem))
        return false;

    // there can be no spaces between the square bracket and string
    prev = "[" + elem;

    while (data_stream >> elem && elem != "}")
        prev = prev + ' ' + elem;

    prev = prev + "]";
    return converter->add_option_to_table(list_name, prev);
}

bool PortScan::parse_list(std::string list_name, std::stringstream& data_stream)
{
    std::string elem;
    bool retval = true;

    if(!(data_stream >> elem) || (elem != "{"))
        return false;

    while (data_stream >> elem && elem != "}")
        retval && converter->add_list_to_table(list_name, elem) && retval;

    return retval;
}

bool PortScan::parse_option(std::string list_name, std::stringstream& data_stream)
{
    std::string elem;
    bool retval = true;

    if(!(data_stream >> elem) || (elem != "{"))
        return false;

    while (data_stream >> elem && elem != "}")
        retval && converter->add_option_to_table(list_name, elem) && retval;

    return retval;
}

bool PortScan::add_portscan_global_option(std::string name, std::stringstream& data_stream)
{
    int val;
    std::string garbage;

    if (!(data_stream >> garbage) || (garbage != "{"))
        return false;

    if (!(data_stream >> val))
        return false;

    converter->close_table();
    converter->open_table("port_scan_global");
    bool retval = converter->add_option_to_table(name, val);
    converter->close_table();
    converter->open_table("port_scan");

    if (!(data_stream >> garbage) || (garbage != "}"))
        return false;

    return retval;
}


bool PortScan::convert(std::stringstream& data_stream)
{
    std::string keyword;
    bool retval = true;
    converter->open_table("port_scan");

    while(data_stream >> keyword)
    {
        if(!keyword.compare("proto"))
        {
            converter->add_deprecated_comment("proto", "protos");
            retval = parse_list("protos", data_stream) && retval;
        }

        if(!keyword.compare("scan_type"))
        {
            converter->add_deprecated_comment("scan_type", "scan_types");
            retval = parse_list("scan_types", data_stream) && retval;
        }
        else if(!keyword.compare("sense_level"))
            retval = parse_option("sense_level", data_stream) && retval;

        else if(!keyword.compare("watch_ip"))
            retval = parse_ip_list("watch_ip", data_stream) && retval;

        else if(!keyword.compare("ignore_scanned"))
            retval = parse_ip_list("ignore_scanners", data_stream) && retval;

        else if(!keyword.compare("ignore_scanners"))
            retval = parse_ip_list("ignore_scanned", data_stream) && retval;

        else if(!keyword.compare("include_midstream"))
            retval = converter->add_option_to_table("include_midstream", true) && retval;

        else if(!keyword.compare("disabled"))
            converter->add_deprecated_comment("disabled");

        else if(!keyword.compare("detect_ack_scans"))
            converter->add_deprecated_comment("detect_ack_scans");

        else if(!keyword.compare("logfile"))
            converter->add_deprecated_comment("logfile");

        else if(!keyword.compare("memcap"))
            retval = add_portscan_global_option("memcap", data_stream) && retval;

        else
            retval = false;
    }


    converter->close_table();
    return retval;    
}


/**************************
 *******  A P I ***********
 **************************/

static ConversionState* ctor(Converter* cv)
{
    return new PortScan(cv);
}

static const ConvertMap preprocessor_sfportscan = 
{
    "sfportscan",
    ctor,
};

const ConvertMap* sfportscan_map = &preprocessor_sfportscan;
