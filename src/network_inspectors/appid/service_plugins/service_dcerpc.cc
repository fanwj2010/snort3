//--------------------------------------------------------------------------
// Copyright (C) 2014-2016 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2005-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// service_dcerpc.cc author Sourcefire Inc.

#include "service_dcerpc.h"
#include "appid_module.h"
#include "application_ids.h"
#include "dcerpc.h"

#include "main/snort_debug.h"
#include "utils/util.h"

#define DCERPC_THRESHOLD    3

#define min(x,y) ((x)<(y) ? (x) : (y))

struct ServiceDCERPCData
{
    unsigned count;
};

static int dcerpc_init(const InitServiceAPI* const init_api);
static int dcerpc_tcp_validate(ServiceValidationArgs* args);
static int dcerpc_udp_validate(ServiceValidationArgs* args);

static const RNAServiceElement tcp_svc_element =
{
    nullptr,
    &dcerpc_tcp_validate,
    nullptr,
    DETECTOR_TYPE_DECODER,
    1,
    1,
    0,
    "dcerpc"
};
static const RNAServiceElement udp_svc_element =
{
    nullptr,
    &dcerpc_udp_validate,
    nullptr,
    DETECTOR_TYPE_DECODER,
    1,
    1,
    0,
    "udp dcerpc"
};

static const RNAServiceValidationPort pp[] =
{
    { &dcerpc_tcp_validate, 135, IpProtocol::TCP, 0 },
    { &dcerpc_udp_validate, 135, IpProtocol::UDP, 0 },
    { nullptr, 0, IpProtocol::PROTO_NOT_SET, 0 }
};

RNAServiceValidationModule dcerpc_service_mod =
{
    "DCERPC",
    &dcerpc_init,
    pp,
    nullptr,
    nullptr,
    0,
    nullptr,
    0
};

static AppRegistryEntry appIdRegistry[] =
{
    { APP_ID_DCE_RPC, 0 }
};

static int dcerpc_init(const InitServiceAPI* const init_api)
{
    unsigned i;
    for (i=0; i < sizeof(appIdRegistry)/sizeof(*appIdRegistry); i++)
    {
        DebugFormat(DEBUG_APPID,"registering appId: %d\n",appIdRegistry[i].appId);
        init_api->RegisterAppId(&dcerpc_udp_validate, appIdRegistry[i].appId,
            appIdRegistry[i].additionalInfo);
    }

    return 0;
}

static int dcerpc_tcp_validate(ServiceValidationArgs* args)
{
    ServiceDCERPCData* dd;
    int retval = SERVICE_INPROCESS;
    int length;
    AppIdSession* asd = args->asd;
    const uint8_t* data = args->data;
    uint16_t size = args->size;

    if (args->dir != APP_ID_FROM_RESPONDER)
        goto inprocess;
    if (!size)
        goto inprocess;

    dd = (ServiceDCERPCData*)dcerpc_service_mod.api->data_get(asd,
        dcerpc_service_mod.flow_data_index);
    if (!dd)
    {
        dd = (ServiceDCERPCData*)snort_calloc(sizeof(ServiceDCERPCData));
        dcerpc_service_mod.api->data_add(asd, dd, dcerpc_service_mod.flow_data_index,
            &snort_free);
    }

    while (size)
    {
        length = dcerpc_validate(data, size);
        if (length < 0)
            goto fail;
        dd->count++;
        if (dd->count >= DCERPC_THRESHOLD)
            retval = SERVICE_SUCCESS;
        data += length;
        size -= length;
    }
    if (retval == SERVICE_SUCCESS)
    {
        dcerpc_service_mod.api->add_service(asd, args->pkt, args->dir, &tcp_svc_element,
            APP_ID_DCE_RPC, nullptr, nullptr, nullptr);
        appid_stats.dcerpc_tcp_flows++;
        return SERVICE_SUCCESS;
    }

inprocess:
    dcerpc_service_mod.api->service_inprocess(asd, args->pkt, args->dir, &tcp_svc_element);
    return SERVICE_INPROCESS;

fail:
    dcerpc_service_mod.api->fail_service(asd, args->pkt, args->dir, &tcp_svc_element,
        dcerpc_service_mod.flow_data_index);
    return SERVICE_NOMATCH;
}

static int dcerpc_udp_validate(ServiceValidationArgs* args)
{
    ServiceDCERPCData* dd;
    int retval = SERVICE_NOMATCH;
    int length;
    AppIdSession* asd = args->asd;
    const uint8_t* data = args->data;
    uint16_t size = args->size;

    if (args->dir != APP_ID_FROM_RESPONDER)
        goto inprocess;
    if (!size)
        goto inprocess;

    dd = (ServiceDCERPCData*)dcerpc_service_mod.api->data_get(asd,
        dcerpc_service_mod.flow_data_index);
    if (!dd)
    {
        dd = (ServiceDCERPCData*)snort_calloc(sizeof(ServiceDCERPCData));
        dcerpc_service_mod.api->data_add(asd, dd, dcerpc_service_mod.flow_data_index,
            &snort_free);
    }

    while (size)
    {
        length = dcerpc_validate(data, size);
        if (length < 0)
            goto fail;
        dd->count++;
        if (dd->count >= DCERPC_THRESHOLD)
            retval = SERVICE_SUCCESS;
        data += length;
        size -= length;
    }
    if (retval == SERVICE_SUCCESS)
    {
        dcerpc_service_mod.api->add_service(asd, args->pkt, args->dir, &udp_svc_element,
            APP_ID_DCE_RPC, nullptr, nullptr, nullptr);
        appid_stats.dcerpc_udp_flows++;
        return SERVICE_SUCCESS;
    }

inprocess:
    dcerpc_service_mod.api->service_inprocess(asd, args->pkt, args->dir, &udp_svc_element);
    return SERVICE_INPROCESS;

fail:
    dcerpc_service_mod.api->fail_service(asd, args->pkt, args->dir, &udp_svc_element,
        dcerpc_service_mod.flow_data_index);
    return SERVICE_NOMATCH;
}

