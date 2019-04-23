/*
 * Amazon FreeRTOS
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */


/**
 * @file aws_iot_demo_network.c
 * @brief Contains implementation for network creation and teardown functions for handling different types of network connections
 */
#include "iot_demo_logging.h"
#include "iot_network_manager_private.h"
#include "aws_iot_demo_network.h"
#include "platform/iot_network_afr.h"
#include "private/iot_error.h"

#if BLE_ENABLED

#include "iot_ble_mqtt.h"

/**
 * @brief Creates a network connection over BLE transport type to transfer MQTT messages.
 * @return true if the connection was created successfully
 */
static BaseType_t prxCreateBLEConnection( MqttConnectionContext_t *pxNetworkContext );
#endif

#if WIFI_ENABLED
/**
 * @brief Creates a secure socket Connection over TCP/IP transport type.
 *
 * @param[in] pxNetworkContext Network context for the connection.
 * @return pdTRUE if the connection is created successfully
 */
static BaseType_t prxCreateSecureSocketConnection( MqttConnectionContext_t *pxNetworkContext );
#endif

/**
 * @brief Creates a network connection to one of the preferred networks
 * @param pxNetworkContext Pointer to the user supplied network context.
 * @param ulPreferredNetworks OR separated flag of preferred neworks.
 * @return
 */
static uint32_t prxCreateNetworkConnection( MqttConnectionContext_t *pxNetworkContext, uint32_t ulNetworkTypes );

/**
 * @brief Used to initialize the network interface.
 */
static IotMqttNetworkInfo_t xDefaultNetworkInterface = IOT_MQTT_NETWORK_INFO_INITIALIZER;

static IotNetworkInterface_t xNetworkInterface =
{
		.create = NULL,
		.send = IotNetworkAfr_Send,
		.receive = IotNetworkAfr_Receive,
		.setReceiveCallback = IotNetworkAfr_SetReceiveCallback,
		.close = NULL,
		.destroy = NULL
};

static uint32_t prxCreateNetworkConnection( MqttConnectionContext_t *pxNetworkContext, uint32_t ulNetworkTypes )
{
    uint32_t ulConnectedNetworks =
            ( AwsIotNetworkManager_GetConnectedNetworks() & ulNetworkTypes );

#if BLE_ENABLED
    if( ( ulConnectedNetworks & AWSIOT_NETWORK_TYPE_BLE ) == AWSIOT_NETWORK_TYPE_BLE )
    {
        if( prxCreateBLEConnection( pxNetworkContext ) == pdTRUE )
        {
            return AWSIOT_NETWORK_TYPE_BLE;
        }
    }
#endif

#if WIFI_ENABLED
    if( ( ulConnectedNetworks & AWSIOT_NETWORK_TYPE_WIFI ) == AWSIOT_NETWORK_TYPE_WIFI )
    {
        if( prxCreateSecureSocketConnection( pxNetworkContext ) == pdTRUE )
        {
            return AWSIOT_NETWORK_TYPE_WIFI;
        }
    }
#endif

#if ETH_ENABLED
    if ( ( ulConnectedNetworks & AWSIOT_NETWORK_TYPE_ETH ) == AWSIOT_NETWORK_TYPE_ETH )
    {
        if ( prxCreateSecureSocketConnection( pxNetworkContext ) == pdTRUE )
        {
            return AWSIOT_NETWORK_TYPE_ETH;
        }
    }
#endif

    return AWSIOT_NETWORK_TYPE_NONE;
}

#if WIFI_ENABLED || ETH_ENABLED
static BaseType_t prxCreateSecureSocketConnection( MqttConnectionContext_t *pxNetworkContext )
{
    _IOT_FUNCTION_ENTRY( BaseType_t, pdTRUE);
    BaseType_t xNetworkCreated = pdFALSE;
    IotNetworkError_t xStatus = IOT_NETWORK_SUCCESS;
    static IotNetworkConnectionAfr_t * pConnection = NULL;
    IotNetworkServerInfoAfr_t xServerInfo = AWS_IOT_NETWORK_SERVER_INFO_AFR_INITIALIZER;
    IotNetworkCredentialsAfr_t xCredentials = AWS_IOT_NETWORK_CREDENTIALS_AFR_INITIALIZER;
    IotMqttNetworkInfo_t* pxNetworkIface = &( pxNetworkContext->xNetworkInfo );

    /* Disable ALPN if not using port 443. */
    if( clientcredentialMQTT_BROKER_PORT != 443 )
    {
        xCredentials.pAlpnProtos = NULL;
    }

    /* Establish a TCP connection to the MQTT server. */
    xStatus =  IotNetworkAfr_Create(&xServerInfo, &xCredentials, (void**)&pConnection);

    if( xStatus != IOT_NETWORK_SUCCESS )
    {
        _IOT_SET_AND_GOTO_CLEANUP( pdFALSE );
    }
    else
    {
        xNetworkCreated = pdTRUE;
    }

    xNetworkInterface.close = pxNetworkContext->xDisconnectCallback;

    ( *pxNetworkIface ) = xDefaultNetworkInterface;
    pxNetworkIface->createNetworkConnection = false;
    pxNetworkIface->u.pNetworkConnection = pConnection;
    pxNetworkIface->pNetworkInterface = &xNetworkInterface;
    pxNetworkContext->pvNetworkConnection = ( void* ) pConnection;

    _IOT_FUNCTION_CLEANUP_BEGIN();

    if( status != pdTRUE )
    {
        if( xNetworkCreated == pdTRUE )
        {
            IotNetworkAfr_Close( pConnection );
            IotNetworkAfr_Destroy( pConnection );
        }
    }

    _IOT_FUNCTION_CLEANUP_END();
}
#endif

#if BLE_ENABLED
static BaseType_t prxCreateBLEConnection( MqttConnectionContext_t *pxNetworkContext )
{
    BaseType_t xStatus = pdFALSE;
    IotMqttNetworkInfo_t* pxNetworkInfo = &( pxNetworkContext->xNetworkInfo );
    static IotBleMqttConnectionType_t * bleConnection = NULL;

    if( IotNetworkBle.create( NULL, NULL, ( void * *) &bleConnection ) == IOT_NETWORK_SUCCESS )
    {
    	pxNetworkInfo->createNetworkConnection = false;
    	pxNetworkInfo->u.pNetworkConnection = (void *)bleConnection;
    	pxNetworkInfo->pNetworkInterface = &IotNetworkBle;
    	pxNetworkInfo->pMqttSerializer = &IotBleMqttSerializer;

        pxNetworkContext->pvNetworkConnection = ( void* ) bleConnection;

        xStatus = pdTRUE;
    }
    return xStatus;
}
#endif


BaseType_t xMqttDemoCreateNetworkConnection(
        MqttConnectionContext_t* pxConnection,
        uint32_t ulNetworkTypes )
{
    BaseType_t xRet = pdFALSE;

	pxConnection->ulNetworkType = prxCreateNetworkConnection( pxConnection, ulNetworkTypes );

	if( pxConnection->ulNetworkType != AWSIOT_NETWORK_TYPE_NONE )
	{
		xRet = pdTRUE;
	}

    return xRet;
}

void vMqttDemoDeleteNetworkConnection( MqttConnectionContext_t* pxNetworkContext )
{
    BaseType_t xDeleted = pdFALSE;

    if( pxNetworkContext != NULL )
    {

#if BLE_ENABLED
        if( pxNetworkContext->ulNetworkType == AWSIOT_NETWORK_TYPE_BLE )
        {
            IotBleMqtt_CloseConnection(  pxNetworkContext->pvNetworkConnection );
            IotBleMqtt_DestroyConnection( pxNetworkContext->pvNetworkConnection );
            xDeleted = pdTRUE;
        }
#endif
#if WIFI_ENABLED
        if(  pxNetworkContext->ulNetworkType  == AWSIOT_NETWORK_TYPE_WIFI )
        {
            IotNetworkAfr_Close(  pxNetworkContext->pvNetworkConnection );
            IotNetworkAfr_Destroy( pxNetworkContext->pvNetworkConnection );
            xDeleted = pdTRUE;
        }
#endif

        if( xDeleted )
        {
            pxNetworkContext->pvNetworkConnection = NULL;
            pxNetworkContext->ulNetworkType = AWSIOT_NETWORK_TYPE_NONE;
        }

    }
}