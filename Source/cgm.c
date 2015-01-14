/**************************************************************************************************
  Filename:       cgm.c

  Revised:        $Date: $
  Revision:       $Revision:  $

  Description:    This file contains the CGM sensor simulator application
                  for use with the CC2540 Bluetooth Low Energy Protocol Stack.

  Copyright 2011-2013 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED �AS IS� WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED,
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE,
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com.
**************************************************************************************************/


/*********************************************************************
 * INCLUDES
 */

#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"
#include "OnBoard.h"
#include "hal_adc.h"
#include "hal_led.h"
#include "hal_key.h"
#include "gatt.h"
#include "hci.h"
#include "gapgattserver.h"
#include "gattservapp.h"
#include "gatt_profile_uuid.h"
#include "linkdb.h"
#include "peripheral.h"
#include "gapbondmgr.h"
#include "cgmservice.h"
#include "devinfoservice.h"
#include "cgm.h"
#include "OSAL_Clock.h"
#include "CGM_Service_values.h"
#include "battservice.h"
#include "math.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// Fast advertising interval in 625us units
#define DEFAULT_FAST_ADV_INTERVAL             32

// Duration of fast advertising duration in sec
#define DEFAULT_FAST_ADV_DURATION             30

// Slow advertising interval in 625us units
#define DEFAULT_SLOW_ADV_INTERVAL             1600

// Duration of slow advertising duration in sec
#define DEFAULT_SLOW_ADV_DURATION             30

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         FALSE

// Minimum connection interval (units of 1.25ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     200

// Maximum connection interval (units of 1.25ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     1600

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         1

// Supervision timeout value (units of 10ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          1000

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         FALSE

// Default passcode
#define DEFAULT_PASSCODE                      19655

// Default GAP pairing mode
#define DEFAULT_PAIRING_MODE                  GAPBOND_PAIRING_MODE_WAIT_FOR_REQ //GAPBOND_PAIRING_MODE_INITIATE [use this]

// Default MITM mode (TRUE to require passcode or OOB when pairing)
#define DEFAULT_MITM_MODE                     TRUE

// Default bonding mode, TRUE to bond
#define DEFAULT_BONDING_MODE                  TRUE

// Default GAP bonding I/O capabilities
#define DEFAULT_IO_CAPABILITIES               GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT

// Notification period in ms
#define DEFAULT_NOTI_PERIOD                   1000


/*********************************************************************
 * TYPEDEFS
 */

// contains the data of control point command

//NEW
//the simplest measurement result
typedef struct {
  uint8         size;
  uint8         flags;
  uint16        concentration;
  uint16        timeoffset;
} cgmMeas_t;

//NEW
typedef struct {
  uint24 cgmFeature;
  uint8  cgmTypeSample;
} cgmFeature_t;

//NEW
typedef struct {
  uint16 timeOffset;
  uint24 cgmStatus;
} cgmStatus_t;

//NEW
typedef struct {
  UTCTimeStruct startTime;
  int8         timeZone;
  uint8         dstOffset;
} cgmSessionStartTime_t;

//NEW
 typedef struct {
  osal_event_hdr_t hdr; //!< MSG_EVENT and status
  uint8 len;
  uint8 data[CGM_CTL_PNT_MAX_SIZE];
} cgmCtlPntMsg_t;


/*********************************************************************
 * GLOBAL VARIABLES
 */

// Task ID
uint8 cgmTaskId;

// Connection handle
uint16 gapConnHandle;

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

// GAP State
static gaprole_States_t gapProfileState = GAPROLE_INIT;

// GAP Profile - Name attribute for SCAN RSP data
static uint8 scanData[] =
{
  0x08,   // length of this data
  0x09,   // AD Type = Complete local name
  'C',
  'G',
  'M',
  ' ',
  'S',
  'i',
  'm'
};

static uint8 advertData[] =
{
  // flags
  0x02,
  GAP_ADTYPE_FLAGS,
  GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
  // service UUIDs
  0x05,
  GAP_ADTYPE_16BIT_MORE,
  LO_UINT16(CGM_SERV_UUID),
  HI_UINT16(CGM_SERV_UUID),
  LO_UINT16(DEVINFO_SERV_UUID),
  HI_UINT16(DEVINFO_SERV_UUID)
};

// Device name attribute value
static uint8 attDeviceName[GAP_DEVICE_NAME_LEN] = "CGM Simulator";

// Bonded state
static bool cgmBonded = FALSE;

// Bonded peer address
static uint8 cgmBondedAddr[B_ADDR_LEN];

// GAP connection handle
static uint16 gapConnHandle;

// Indication structures for cgm
//NEW
static attHandleValueInd_t cgmCtlPntRsp;
static attHandleValueNoti_t  CGMMeas;
static attHandleValueInd_t   cgmCtlPntRsp;

// Advertising user-cancelled state
static bool cgmAdvCancelled = FALSE;

//the most current measurement
static cgmMeas_t        cgmCurrentMeas;

//NEW - for the cgm history record
//static cgmMeas_t        cgmMeasDB[CGM_MEAS_DB_SIZE];
//static uint8            cgmMeasDBWriteIndex=0; //pointing to the most current cgm record
//static uint8            cgmMeasDBCount=0;

//new all the variables needed for the cgm simulator
//the support feature
static uint16                   cgmCommInterval=1000;//the communication interval in ms
static cgmFeature_t             cgmFeature={CGM_FEATURE_MULTI_BOND | CGM_FEATURE_E2E_CRC | CGM_FEATURE_CAL, BUILD_UINT8(CGM_TYPE_ISF,CGM_SAMPLE_LOC_SUBCUT_TISSUE)};
static cgmStatus_t              cgmStatus={0x1234,0x567890}; //for testing purpose only
static cgmSessionStartTime_t    cgmStartTime={{20,3,3,8,1,2015},TIME_ZONE_UTC_M5,DST_STANDARD_TIME};
static uint16                   cgmSessionRunTime=0x1234;
static UTCTime                  cgmCurrentTime_UTC;     //the UTC format of the current start time

/*
static UTCTimeStruct            cgmCurrentTime;
static uint16                   cgmOffsetTime;//this can be derived from subtracting start time from current time
static bool                     cgmSesStartIndicator=false;
static bool                     cgmSensorMalfunctionIndicator=false;
static uint8                    cgmBatteryLevel=95;//battery level in percentage
static uint16                   cgmCurrentMeas=0x0123;//the most recent cgm measurement
static uint16                   cgmCalibration=0x0222;//the most recent calibration
static uint16                   cgmHypoAlert=0x0333;//the alert level for hypo condition
static uint16                   cgmHyperAlert=0x0444;//the alert level for hypo condition
static uint16                   cgmHighAlert=0x0555;//the alert level for patient high condition
static uint16                   cgmLowAlert=0x0666;//the alert level for patient low condition
static uint16                   cgmChangeRate=0x0777;//the rate of change of cgm
static uint16                   cgmSensorTemperature=0x888;//the temperature of sensor
static uint16                   cgmQuality=0x0999;      //the quality of CGM data
*/

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void cgm_ProcessOSALMsg( osal_event_hdr_t *pMsg );
static void cgmProcessCtlPntMsg( cgmCtlPntMsg_t* pMsg);
static void cgmGapStateCB( gaprole_States_t newState );
static void cgm_HandleKeys( uint8 shift, uint8 keys );
static void cgmMeasSend(void);
static uint8 cgmVerifyTime(UTCTimeStruct* pTime);
static void cgmCtlPntResponse(uint8 opcode, uint8 rspcode);
static void cgmservice_cb(uint8 event, uint8* valueP, uint8 len);
static void cgmPasscodeCB( uint8 *deviceAddr, uint16 connectionHandle,
                                        uint8 uiInputs, uint8 uiOutputs );
static void cgmPairStateCB( uint16 connHandle, uint8 state, uint8 status );



/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t cgm_PeripheralCBs =
{
  cgmGapStateCB,  // Profile State Change Callbacks
  NULL                // When a valid RSSI is read from controller
};

// Bond Manager Callbacks
static const gapBondCBs_t cgmBondCB =
{
  cgmPasscodeCB,
  cgmPairStateCB
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      CGM_Init
 *
 * @brief   Initialization function for the CGM App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by OSAL.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void CGM_Init( uint8 task_id )
{
  cgmTaskId = task_id;

  // Setup the GAP Peripheral Role Profile
  {
    #if defined( CC2540_MINIDK )
      // For the CC2540DK-MINI keyfob, device doesn't start advertising until button is pressed
      uint8 initial_advertising_enable = TRUE;
    #else
      // For other hardware platforms, device starts advertising upon initialization
      uint8 initial_advertising_enable = TRUE;
    #endif
    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    uint16 gapRole_AdvertOffTime = 0;

    uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

    // Set the GAP Role Parameters
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
    GAPRole_SetParameter( GAPROLE_ADVERT_OFF_TIME, sizeof( uint16 ), &gapRole_AdvertOffTime );

    GAPRole_SetParameter( GAPROLE_SCAN_RSP_DATA, sizeof ( scanData ), scanData );
    GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );

    GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
    GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
    GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
    GAPRole_SetParameter( GAPROLE_SLAVE_LATENCY, sizeof( uint16 ), &desired_slave_latency );
    GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );
  }

  // Set the GAP Characteristics
  GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName );

  // Setup the GAP Bond Manager
  {
    uint32 passkey = DEFAULT_PASSCODE;
    uint8 pairMode = DEFAULT_PAIRING_MODE;
    uint8 mitm = DEFAULT_MITM_MODE;
    uint8 ioCap = DEFAULT_IO_CAPABILITIES;
    uint8 bonding = DEFAULT_BONDING_MODE;

    GAPBondMgr_SetParameter( GAPBOND_DEFAULT_PASSCODE, sizeof ( uint32 ), &passkey );
    GAPBondMgr_SetParameter( GAPBOND_PAIRING_MODE, sizeof ( uint8 ), &pairMode );
    GAPBondMgr_SetParameter( GAPBOND_MITM_PROTECTION, sizeof ( uint8 ), &mitm );
    GAPBondMgr_SetParameter( GAPBOND_IO_CAPABILITIES, sizeof ( uint8 ), &ioCap );
    GAPBondMgr_SetParameter( GAPBOND_BONDING_ENABLED, sizeof ( uint8 ), &bonding );
  }

  // Initialize GATT Client
  VOID GATT_InitClient();

  // Register to receive incoming ATT Indications/Notifications
  GATT_RegisterForInd( cgmTaskId );

  // Initialize GATT attributes
  GGS_AddService( GATT_ALL_SERVICES );         // GAP
  GATTServApp_AddService( GATT_ALL_SERVICES ); // GATT attributes
  CGM_AddService(GATT_ALL_SERVICES);
  DevInfo_AddService( );
  Batt_AddService();                              // Battery Service
    
  // Register for CGM service callback
  CGM_Register ( cgmservice_cb);

  // Register for all key events - This app will handle all key events
  RegisterForKeys( cgmTaskId );

 #if defined( CC2540_MINIDK )
  // makes sure LEDs are off
  HalLedSet( (HAL_LED_1 | HAL_LED_2), HAL_LED_MODE_OFF );

  // For keyfob board set GPIO pins into a power-optimized state
  // Note that there is still some leakage current from the buzzer,
  // accelerometer, LEDs, and buttons on the PCB.

  P0SEL = 0; // Configure Port 0 as GPIO
  P1SEL = 0; // Configure Port 1 as GPIO
  P2SEL = 0; // Configure Port 2 as GPIO

  P0DIR = 0xFC; // Port 0 pins P0.0 and P0.1 as input (buttons),
                // all others (P0.2-P0.7) as output
  P1DIR = 0xFF; // All port 1 pins (P1.0-P1.7) as output
  P2DIR = 0x1F; // All port 1 pins (P2.0-P2.4) as output

  P0 = 0x03; // All pins on port 0 to low except for P0.0 and P0.1 (buttons)
  P1 = 0;   // All pins on port 1 to low
  P2 = 0;   // All pins on port 2 to low

#endif // #if defined( CC2540_MINIDK )

  // Setup a delayed profile startup
  osal_set_event( cgmTaskId, START_DEVICE_EVT );
  
}

/*********************************************************************
 * @fn      CGM_ProcessEvent
 *
 * @brief   CGM Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16 CGM_ProcessEvent( uint8 task_id, uint16 events )
{

  VOID task_id; // OSAL required parameter that isn't used in this function

  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;

    if ( (pMsg = osal_msg_receive( cgmTaskId )) != NULL )
    {
      cgm_ProcessOSALMsg( (osal_event_hdr_t *)pMsg );

      // Release the OSAL message
      VOID osal_msg_deallocate( pMsg );
    }

    // return unprocessed events
    return (events ^ SYS_EVENT_MSG);
  }

  if ( events & START_DEVICE_EVT )
  {
    // Start the Device
    VOID GAPRole_StartDevice( &cgm_PeripheralCBs );

    // Register with bond manager after starting device
    GAPBondMgr_Register( (gapBondCBs_t *) &cgmBondCB );
    // Start the notification
  
    return ( events ^ START_DEVICE_EVT );
  }
//NEW
  if ( events & NOTI_TIMEOUT_EVT )
  {
    // Send the current value of the CGM reading
    cgmMeasSend();
    return ( events ^ NOTI_TIMEOUT_EVT );
  }

  return 0;
}

/*********************************************************************
 * @fn      cgm_ProcessOSALMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void cgm_ProcessOSALMsg( osal_event_hdr_t *pMsg )
{
  switch ( pMsg->event )
  {
  case KEY_CHANGE:
      cgm_HandleKeys( ((keyChange_t *)pMsg)->state, ((keyChange_t *)pMsg)->keys );
      break;

  case CTL_PNT_MSG:
      cgmProcessCtlPntMsg( (cgmCtlPntMsg_t *) pMsg);
      break;

  default:
      break;
  }
}

/*********************************************************************
 * @fn      cgm_HandleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 HAL_KEY_SW_2
 *                 HAL_KEY_SW_1
 *
 * @return  none
 */
static void cgm_HandleKeys( uint8 shift, uint8 keys )
{
  if ( keys & HAL_KEY_SW_1 )
  {
    
  }

  if ( keys & HAL_KEY_SW_2 )
  {
    // if device is not in a connection, pressing the right key should toggle
    // advertising on and off
    if( gapProfileState != GAPROLE_CONNECTED )
    {
      uint8 status;

      // Set fast advertising interval for user-initiated connections
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, DEFAULT_FAST_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, DEFAULT_FAST_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_ADV_TIMEOUT, DEFAULT_FAST_ADV_DURATION );

      // toggle GAP advertisement status
      GAPRole_GetParameter( GAPROLE_ADVERT_ENABLED, &status );
      status = !status;
      GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &status );

      // Set state variable
      if (status == FALSE)
      {
        cgmAdvCancelled = TRUE;
      }
    }
  }
}


/*********************************************************************
 * @fn      cgmProcessCtlPntMsg
 *
 * @brief   Process Control Point messages
 *
 * @return  none
 */
//NEW
static void cgmProcessCtlPntMsg (cgmCtlPntMsg_t * pMsg)
{
  uint8 opcode = pMsg->data[0];
  uint8 ropcode; //the op code in the return char value
  uint8 rspcode; //the response code in the return char value
  uint8 *operand;//the operand in eith the input or reuturn char value
  uint8 operand_len; // the operand length
 
  switch(opcode)
  { //currently only implement the set/get communication interval
    case CGM_SPEC_OP_GET_INTERVAL:
          ropcode=CGM_SPEC_OP_RESP_INTERVAL;
          rspcode=CGM_SPEC_OP_RESP_SUCCESS;
          cgmCtlPntRsp.len=sizeof(cgmCommInterval)+2;
          osal_memcpy(cgmCtlPntRsp.value+2,(uint8 *)&cgmCommInterval,sizeof(cgmCommInterval));
          break;
          
    case CGM_SPEC_OP_SET_INTERVAL:
          ropcode=CGM_SPEC_OP_RESP_CODE;
          operand=pMsg->data+2;
          operand_len=pMsg->len-2;
          if (operand_len!=1) //the input interval assumes 1 byte
            rspcode=CGM_SPEC_OP_RESP_OPERAND_INVALID;
          else
          {

            if ((*operand)==0) //input value being 0x00 would stop the timer.
              osal_stop_timerEx(cgmTaskId,NOTI_TIMEOUT_EVT);
            else              
              cgmCommInterval=1000*(0xFF-*operand+1); // in ms
            rspcode=CGM_SPEC_OP_RESP_SUCCESS; 
          }
          cgmCtlPntRsp.len=2;
          break;
  default:
          break;
  }
  cgmCtlPntResponse(ropcode,rspcode);
          
}


/*********************************************************************
 * @fn      cgmGapStateCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void cgmGapStateCB( gaprole_States_t newState )
{
  // if connected
  if ( newState == GAPROLE_CONNECTED )
  {

  }
  // if disconnected
  else if (gapProfileState == GAPROLE_CONNECTED &&
           newState != GAPROLE_CONNECTED)
  {
    uint8 advState = TRUE;

    // stop notification timer
    osal_stop_timerEx(cgmTaskId, NOTI_TIMEOUT_EVT);

    if ( newState == GAPROLE_WAITING_AFTER_TIMEOUT )
    {
      // link loss timeout-- use fast advertising
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, DEFAULT_FAST_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, DEFAULT_FAST_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_ADV_TIMEOUT, DEFAULT_FAST_ADV_DURATION );
    }
    else
    {
      // Else use slow advertising
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, DEFAULT_SLOW_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, DEFAULT_SLOW_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_ADV_TIMEOUT, DEFAULT_SLOW_ADV_DURATION );
    }

    // Enable advertising
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &advState );
  }
  // if advertising stopped
  else if ( gapProfileState == GAPROLE_ADVERTISING &&
            newState == GAPROLE_WAITING )
  {
    // if advertising stopped by user
    if ( cgmAdvCancelled )
    {
      cgmAdvCancelled = FALSE;
    }
    // if fast advertising switch to slow
    else if ( GAP_GetParamValue( TGAP_LIM_DISC_ADV_INT_MIN ) == DEFAULT_FAST_ADV_INTERVAL )
    {
      uint8 advState = TRUE;

      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, DEFAULT_SLOW_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, DEFAULT_SLOW_ADV_INTERVAL );
      GAP_SetParamValue( TGAP_LIM_ADV_TIMEOUT, DEFAULT_SLOW_ADV_DURATION );
      GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &advState );
    }
  }
  // if started
  else if ( newState == GAPROLE_STARTED )
  {
    // Set the system ID from the bd addr
    uint8 systemId[DEVINFO_SYSTEM_ID_LEN];
    GAPRole_GetParameter(GAPROLE_BD_ADDR, systemId);

    // shift three bytes up
    systemId[7] = systemId[5];
    systemId[6] = systemId[4];
    systemId[5] = systemId[3];

    // set middle bytes to zero
    systemId[4] = 0;
    systemId[3] = 0;

    DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);
  }
  gapProfileState = newState;
}

/*********************************************************************
 * @fn      cgmPairStateCB
 *
 * @brief   Pairing state callback.
 *
 * @return  none
 */
static void cgmPairStateCB( uint16 connHandle, uint8 state, uint8 status )
{
  if ( state == GAPBOND_PAIRING_STATE_COMPLETE )
  {
    if ( status == SUCCESS )
    {
      linkDBItem_t  *pItem;

      if ( (pItem = linkDB_Find( gapConnHandle )) != NULL )
      {
        // Store bonding state of pairing
        cgmBonded = ( (pItem->stateFlags & LINK_BOUND) == LINK_BOUND );

        if ( cgmBonded )
        {
          osal_memcpy( cgmBondedAddr, pItem->addr, B_ADDR_LEN );
        }
      }
    }
  }
}

/*********************************************************************
 * @fn      cgmPasscodeCB
 *
 * @brief   Passcode callback.
 *
 * @return  none
 */
static void cgmPasscodeCB( uint8 *deviceAddr, uint16 connectionHandle,
                                        uint8 uiInputs, uint8 uiOutputs )
{
  // Send passcode response
  GAPBondMgr_PasscodeRsp( connectionHandle, SUCCESS, DEFAULT_PASSCODE );
}

/*********************************************************************
 * @fn      CGMMeasSend
 *
 * @brief   Prepare and send a CGM measurement
 *
 * @return  none
 */
//NEW
static void cgmMeasSend(void)
{
  //manually change the CGM measurement value
  cgmCurrentMeas.size=6;
  cgmCurrentMeas.flags=0;
  cgmCurrentMeas.concentration++;
  cgmCurrentMeas.timeoffset++;
  
  //att value notification structure
  uint8 *p=CGMMeas.value;
  uint8 flags=cgmCurrentMeas.flags;
  
  //load data into the package buffer
  *p++ = cgmCurrentMeas.size;
  *p++ = flags;
  *p++ = LO_UINT16(cgmCurrentMeas.concentration);
  *p++ = HI_UINT16(cgmCurrentMeas.concentration);
  *p++ = LO_UINT16(cgmCurrentMeas.timeoffset);
  *p++ = HI_UINT16(cgmCurrentMeas.timeoffset);
  CGMMeas.len=(uint8)(p-CGMMeas.value);
  
  //Send a measurement
  CGM_MeasSend(gapConnHandle, &CGMMeas,  cgmTaskId);
  
  //Restart timer
  osal_start_timerEx(cgmTaskId, NOTI_TIMEOUT_EVT, cgmCommInterval);
}

//NEW
/*********************************************************************
 * @fn      cgmCtlPntResponse
 *
 * @brief   Send a record control point response
 *
 * @param   opcode - cgm specific control point opcode
 * @param   rspcode - response code
 *
 * @return  none
 */
static void cgmCtlPntResponse(uint8 opcode, uint8 rspcode)
{
  cgmCtlPntRsp.value[0]=opcode;
  cgmCtlPntRsp.value[1]=rspcode;
  CGM_CtlPntIndicate(gapConnHandle, &cgmCtlPntRsp,  cgmTaskId);
}


/*********************************************************************
 * @fn      cgmservice_cb
 *
 * @brief   Callback function from app to service.
 *
 * @param   event - service event
 *
 * @return  none
 */

//NEW
static void cgmservice_cb(uint8 event, uint8* valueP, uint8 len)
{

  switch (event)
  {
  case CGM_MEAS_NTF_ENABLED:
    {
        osal_start_timerEx(cgmTaskId, NOTI_TIMEOUT_EVT, DEFAULT_NOTI_PERIOD);
        break;
    }
  case CGM_MEAS_NTF_DISABLED:
    {
        osal_stop_timerEx(cgmTaskId, NOTI_TIMEOUT_EVT);
        break;
    }
    
   
  case CGM_FEATURE_READ_REQUEST:
    {
        *valueP = cgmFeature.cgmFeature & 0xFF;
        *(++valueP) = (cgmFeature.cgmFeature >> 8) & 0xFF;
        *(++valueP) = (cgmFeature.cgmFeature >> 16) & 0xFF;
        *(++valueP) =  cgmFeature.cgmTypeSample;
        break;
    }
   
   case CGM_STATUS_READ_REQUEST:
     {
        *valueP = LO_UINT16(cgmStatus.timeOffset);
        *(++valueP) = HI_UINT16(cgmStatus.timeOffset);
        *(++valueP) = BREAK_UINT32(cgmStatus.cgmStatus,0);
        *(++valueP) = BREAK_UINT32(cgmStatus.cgmStatus,1);
        *(++valueP) = BREAK_UINT32(cgmStatus.cgmStatus,2);
        break;
     }
    
  case CGM_START_TIME_READ_REQUEST:
    {
        *valueP = (cgmStartTime.startTime.year & 0xFF);
        *(++valueP) = (cgmStartTime.startTime.year >> 8) & 0xFF;
        *(++valueP) = (cgmStartTime.startTime.month) & 0xFF;
        *(++valueP) = (cgmStartTime.startTime.day) & 0xFF;
        *(++valueP) = (cgmStartTime.startTime.hour) & 0xFF;
        *(++valueP) = (cgmStartTime.startTime.minutes) & 0xFF;
        *(++valueP) = (cgmStartTime.startTime.seconds) & 0xFF;
        *(++valueP) = (cgmStartTime.timeZone) & 0xFF;
        *(++valueP) = (cgmStartTime.dstOffset) & 0xFF;
        break;
    }
   
  case CGM_RUN_TIME_READ_REQUEST:
    {
        *valueP = cgmSessionRunTime & 0xFF;
        *(++valueP) = (cgmSessionRunTime >> 8) & 0xFF;
        break;
    }
    
  case CGM_START_TIME_WRITE_REQUEST:
    {
      cgmStartTime.startTime.year=BUILD_UINT16(valueP[0],valueP[1]);
      cgmStartTime.startTime.month=valueP[2];
      cgmStartTime.startTime.day=valueP[3];
      cgmStartTime.startTime.hour=valueP[4];
      cgmStartTime.startTime.minutes=valueP[5];
      cgmStartTime.startTime.seconds=valueP[6];
      cgmStartTime.timeZone=valueP[7];
      cgmStartTime.dstOffset=valueP[8];
      cgmCurrentTime_UTC=osal_ConvertUTCSecs(&cgmStartTime.startTime);//convert to second format
      osal_setClock(cgmCurrentTime_UTC);//set the system clock to the session start time
      break;
    }

  case CGM_CTL_PNT_CMD:
    {
      cgmCtlPntMsg_t* msgPtr;
      // Send the address to the task list
      msgPtr = (cgmCtlPntMsg_t *)osal_msg_allocate( sizeof(cgmCtlPntMsg_t) );
      if ( msgPtr )
      {
        msgPtr->hdr.event = CTL_PNT_MSG;
        msgPtr->len = len;
        osal_memcpy(msgPtr->data, valueP, len);
        osal_msg_send( cgmTaskId, (uint8 *)msgPtr );
      }
    }
      break;
    
  default:
    break;
  }
}

/*********************************************************************
* @fn cgmVerifyTime
*
* @brief Verify time values are suitable for filtering
*
* @param pTime - UTC time struct
*
* @return true if time is ok, false otherwise
*/
static uint8 cgmVerifyTime(UTCTimeStruct* pTime)
{
  // sanity check year
  if (pTime->year < 2000 || pTime->year > 2111)
  {
    return false;
  }
  // check day range
  if (pTime->day == 0 || pTime->day > 31)
  {
    return false;
  }
  // check month range
  if (pTime->month == 0 || pTime->month > 12)
  {
    return false;
  }

  // adjust month and day; utc time uses 0-11 and 0-30, characteristic uses 1-12 and 1-31
  pTime->day--;
  pTime->month--;

  return true;
}

/*********************************************************************
*********************************************************************/