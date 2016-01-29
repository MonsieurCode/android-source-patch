/* //device/system/altair-ril/altair-ril.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
/*  ---------------------------------------------------------------------------

    This program has been rewritten or modified by Altair Semiconductor, Ltd.

    (c) copyright 2014 Altair Semiconductor, Ltd.

   ------------------------------------------------------------------------- */



#include <telephony/ril_cdma_sms.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <termios.h>
#include <sys/system_properties.h>

#include "ril.h"
#include "hardware/qemu_pipe.h"

// Altair 3gpp2 (CDMA) sms support header
#include "SMS3GPP2.h"

#define LOG_TAG "RIL_ALT"
#include <utils/Log.h>
#ifndef 	RIL_EMULATE_4_2
#include <telephony/librilutils.h>
#else
#define RLOGD	ALOGD
#define RLOGW	ALOGW
#define RLOGE	ALOGE
#define RLOGI	ALOGI
#endif

#define MAX_AT_RESPONSE 0x1000


void *onUnsolicitedAtSocketUserdata = NULL;
void altairAtSocketOnUnsolicited(const char *s, void *userdata);
int altairAtSocketCreate();

#include <netutils/ifc.h>
#include <netutils/dhcp.h>

#ifdef USE_TI_COMMANDS

// Enable a workaround
// 1) Make incoming call, do not answer
// 2) Hangup remote end
// Expected: call should disappear from CLCC line
// Actual: Call shows as "ACTIVE" before disappearing
#define WORKAROUND_ERRONEOUS_ANSWER 1

// Some varients of the TI stack do not support the +CGEV unsolicited
// response. However, they seem to send an unsolicited +CME ERROR: 150
#define WORKAROUND_FAKE_CGEV 1
#endif

/* Modem Technology bits */
#define MDM_GSM         0x01
#define MDM_WCDMA       0x02
#define MDM_CDMA        0x04
#define MDM_EVDO        0x08
#define MDM_LTE         0x10

typedef struct {
    int supportedTechs; // Bitmask of supported Modem Technology bits
    int currentTech;    // Technology the modem is currently using (in the format used by modem)
    int isMultimode;

    // Preferred mode bitmask. This is actually 4 byte-sized bitmasks with different priority values,
    // in which the byte number from LSB to MSB give the priority.
    //
    //          |MSB|   |   |LSB
    // value:   |00 |00 |00 |00
    // byte #:  |3  |2  |1  |0
    //
    // Higher byte order give higher priority. Thus, a value of 0x0000000f represents
    // a preferred mode of GSM, WCDMA, CDMA, and EvDo in which all are equally preferrable, whereas
    // 0x00000201 represents a mode with GSM and WCDMA, in which WCDMA is preferred over GSM
    int32_t preferredNetworkMode;
    int subscription_source;

} ModemInfo;

static ModemInfo *sMdmInfo;
// TECH returns the current technology in the format used by the modem.
// It can be used as an l-value
#define TECH(mdminfo)                 ((mdminfo)->currentTech)
// TECH_BIT returns the bitmask equivalent of the current tech
#ifdef altair_remove
#define TECH_BIT(mdminfo)            (1 << ((mdminfo)->currentTech))
#else
	// this part of code seems to have bug , in our inplementation the values in mdminfo will be MDM_LTE         0x10
#define	TECH_BIT(mdminfo)				TECH(mdminfo)
#endif
#define IS_MULTIMODE(mdminfo)         ((mdminfo)->isMultimode)
#define TECH_SUPPORTED(mdminfo, tech) ((mdminfo)->supportedTechs & (tech))
#define PREFERRED_NETWORK(mdminfo)    ((mdminfo)->preferredNetworkMode)
// CDMA Subscription Source
#define SSOURCE(mdminfo)              ((mdminfo)->subscription_source)

static int net2modem[] = {
    MDM_GSM | MDM_WCDMA,                                 // 0  - GSM / WCDMA Pref
    MDM_GSM,                                             // 1  - GSM only
    MDM_WCDMA,                                           // 2  - WCDMA only
    MDM_GSM | MDM_WCDMA,                                 // 3  - GSM / WCDMA Auto
    MDM_CDMA | MDM_EVDO,                                 // 4  - CDMA / EvDo Auto
    MDM_CDMA,                                            // 5  - CDMA only
    MDM_EVDO,                                            // 6  - EvDo only
    MDM_GSM | MDM_WCDMA | MDM_CDMA | MDM_EVDO,           // 7  - GSM/WCDMA, CDMA, EvDo
    MDM_LTE | MDM_CDMA | MDM_EVDO,                       // 8  - LTE, CDMA and EvDo
    MDM_LTE | MDM_GSM | MDM_WCDMA,                       // 9  - LTE, GSM/WCDMA
    MDM_LTE | MDM_CDMA | MDM_EVDO | MDM_GSM | MDM_WCDMA, // 10 - LTE, CDMA, EvDo, GSM/WCDMA
    MDM_LTE,                                             // 11 - LTE only
};

static int32_t net2pmask[] = {
    MDM_GSM | (MDM_WCDMA << 8),                          // 0  - GSM / WCDMA Pref
    MDM_GSM,                                             // 1  - GSM only
    MDM_WCDMA,                                           // 2  - WCDMA only
    MDM_GSM | MDM_WCDMA,                                 // 3  - GSM / WCDMA Auto
    MDM_CDMA | MDM_EVDO,                                 // 4  - CDMA / EvDo Auto
    MDM_CDMA,                                            // 5  - CDMA only
    MDM_EVDO,                                            // 6  - EvDo only
    MDM_GSM | MDM_WCDMA | MDM_CDMA | MDM_EVDO,           // 7  - GSM/WCDMA, CDMA, EvDo
    MDM_LTE | MDM_CDMA | MDM_EVDO,                       // 8  - LTE, CDMA and EvDo
    MDM_LTE | MDM_GSM | MDM_WCDMA,                       // 9  - LTE, GSM/WCDMA
    MDM_LTE | MDM_CDMA | MDM_EVDO | MDM_GSM | MDM_WCDMA, // 10 - LTE, CDMA, EvDo, GSM/WCDMA
    MDM_LTE,                                             // 11 - LTE only
};

static int is3gpp2(int radioTech) {
	char  propValue[PROP_VALUE_MAX];
	// if lteOnCdma defined use it to make decision , otherwise use radioTech
	if (__system_property_get("telephony.lteOnCdmaDevice", propValue) != 0) {
		if (propValue[0]=='1'){
			RLOGI("lteOnCdma is enabled , use 3gpp2");
	        return 1;
		} else{
			RLOGE("lteOnCdma(%4s)is available but its value is not 1 , use 3gpp",propValue);
			return 0;
		}
	}
    switch (radioTech) {
        case RADIO_TECH_IS95A:
        case RADIO_TECH_IS95B:
        case RADIO_TECH_1xRTT:
        case RADIO_TECH_EVDO_0:
        case RADIO_TECH_EVDO_A:
        case RADIO_TECH_EVDO_B:
        case RADIO_TECH_EHRPD:
            return 1;
        default:
            return 0;
    }
}

typedef enum {
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2, /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5,
    RUIM_ABSENT = 6,
    RUIM_NOT_READY = 7,
    RUIM_READY = 8,
    RUIM_PIN = 9,
    RUIM_PUK = 10,
    RUIM_NETWORK_PERSONALIZATION = 11
} SIM_Status;

static void onRequest (int request, void *data, size_t datalen, RIL_Token t);
static RIL_RadioState currentState();
static int onSupports (int requestCode);
static void onCancel (RIL_Token t);
static const char *getVersion();
static int isRadioOn();
static SIM_Status getSIMStatus();
static int getCardStatus(RIL_CardStatus_v6 **pp_card_status);
static void freeCardStatus(RIL_CardStatus_v6 *p_card_status);
static void onDataCallListChanged(void *param);
int isDelayAtChannel();

extern const char * requestToString(int request);

// the following defines a helprers for backword compatability for android 4.3 and android 4.2
#ifdef RIL_EMULATE_4_2
#define RIL_VERSION_4_3_AND_ABOVE  0
#define RIL_VERSION_4_4_AND_ABOVE  0
#define RIL_VERSION_SUPPORT  7
#endif

#ifdef RIL_EMULATE_4_3
#define RIL_VERSION_4_3_AND_ABOVE  1
#define RIL_VERSION_4_4_AND_ABOVE  0
#define RIL_VERSION_SUPPORT  8
#endif

#ifndef  RIL_VERSION_4_3_AND_ABOVE
#define RIL_VERSION_4_3_AND_ABOVE  1
#define RIL_VERSION_4_4_AND_ABOVE  1
#define RIL_VERSION_SUPPORT  RIL_VERSION
#endif

static const RIL_RadioFunctions s_callbacks = {
	RIL_VERSION_SUPPORT,
    onRequest,
    currentState,
    onSupports,
    onCancel,
    getVersion
};

#ifdef RIL_SHLIB
static const struct RIL_Env *s_rilenv;

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t,e, response, responselen)
#define RIL_onUnsolicitedResponse(a,b,c) s_rilenv->OnUnsolicitedResponse(a,b,c)
#define RIL_requestTimedCallback(a,b,c) s_rilenv->RequestTimedCallback(a,b,c)
#endif

static RIL_RadioState sState = RADIO_STATE_UNAVAILABLE;

static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;

static int s_port = -1;
static char *s_remote_ip_addr = NULL;
static const char * s_device_path = NULL;
static int          s_device_socket = 0;

/* trigger change to this with s_state_cond */
static int s_closed = 0;

static int sFD;     /* file desc of AT channel */
static char sATBuffer[MAX_AT_RESPONSE+1];
static char *sATBufferCur = NULL;

static const struct timeval TIMEVAL_SIMPOLL = {1,0};
static const struct timeval TIMEVAL_CALLSTATEPOLL = {0,500000};
static const struct timeval TIMEVAL_0 = {0,0};

#ifdef WORKAROUND_ERRONEOUS_ANSWER
// Max number of times we'll try to repoll when we think
// we have a AT+CLCC race condition
#define REPOLL_CALLS_COUNT_MAX 4

// Line index that was incoming or waiting at last poll, or -1 for none
static int s_incomingOrWaitingLine = -1;
// Number of times we've asked for a repoll of AT+CLCC
static int s_repollCallsCount = 0;
// Should we expect a call to be answered in the next CLCC?
static int s_expectAnswer = 0;
#endif /* WORKAROUND_ERRONEOUS_ANSWER */

static int s_cell_info_rate_ms = INT_MAX;
static int s_mcc = 0;
static int s_mnc = 0;
static int s_lac = 0;
static int s_cid = 0;

static void pollSIMState (void *param);
static void setRadioState(RIL_RadioState newState);
#ifdef altair_remove
static void setRadioTechnology(ModemInfo *mdm, int newtech);
static int query_ctec(ModemInfo *mdm, int *current, int32_t *preferred);
static int parse_technology_response(const char *response, int *current, int32_t *preferred);
#endif
static int techFromModemType(int mdmtype);
static int activateDataSessionPdn(int sessionId);

/*
 * the followings are structures and functions declaration added by Altair
 */
// the PdnTable stucture is used as function API for APN names and functionality as defined in the modem.

#define MAX_NUM_OF_PDN 4
#define MAX_APN_NAME 64+1
typedef struct {
	int sessionId;
	int cid;
	int active;
	char APN[MAX_APN_NAME];
}PdnTableEntry;

typedef struct {
	int numOfEntries;
    PdnTableEntry pdn[MAX_NUM_OF_PDN];
}PdnTable;

// pathname returned from RIL_REQUEST_SETUP_DATA_CALL / RIL_REQUEST_SETUP_DEFAULT_PDP - by session id (starts at 1)
// the ETH_IF_NAME should be synchronized with the  APNtable configuration in the NPE Modem.
static char* ETH_IF_NAME[MAX_NUM_OF_PDN]={"NA","eth1","eth1.4","eth1.5"};
#define IFNAME(sessionid) (( sessionid>0)&&(sessionid<4))  ? ETH_IF_NAME[sessionid] : ETH_IF_NAME[0]
typedef enum {
    MEAS_RSRP = 0,
    MEAS_RSRQ ,
    MEAS_SINR ,
    MEAS_RSSI ,
    MEAS_RSSNR,
    MEAS_CQI,
    MEAS_CSQ
} MEAS_Type;

static const char* MEAS_TYPE_STR_CONST[] = 
{
    "MEAS_RSRP",
    "MEAS_RSRQ" ,
    "MEAS_SINR" ,
    "MEAS_RSSI" ,
    "MEAS_RSSNR",
    "MEAS_CQI",
    "MEAS_CSQ"
};

static int requestSignalStrength_helper(MEAS_Type type, int* responseVal);
#define IF_STATE_UP 	1
#define IF_STATE_DOWN 	2
int ifc_config(char* ifName, int ifState );
static int getPdnTable(PdnTable *pdnTblp);
int getPdnByApnName(char *apn,PdnTableEntry* pdn);
static int disconnectLTE();
static int connectLTE();
static int getConnectModeAndPlmnName(int* mode, char* plmnName);
void requestSetNetworkSelectionAutomatic(void *data, size_t datalen, RIL_Token t);
static int isGSMSmsUnsolicited(const char *s,const char *sms_pdu);
static void requestCDMASMSAcknowledge(void *data, size_t datalen, RIL_Token t);
void requestSetNetworkSelectionManual(void *data, size_t datalen, RIL_Token t);
void requestQueryAvailableNetworks(void *data, size_t datalen, RIL_Token t);
#ifdef RIL_FEATURE_ENABLE_VOLTE
void requestGetLastCallFailCause(void *data, size_t datalen, RIL_Token t);
void requestGetClip(void *data, size_t datalen, RIL_Token t);
void requestSetClir(void *data, size_t datalen, RIL_Token t);
void requestQuaryCallWaiting(void *data, size_t datalen, RIL_Token t);
void requestSetCallWaiting(void *data, size_t datalen, RIL_Token t);
#endif //RIL_FEATURE_ENABLE_VOLTE


// altair_todo should be moved to at_channel ?
#define NUM_OF_RETRANSMIT 5
int at_send_command_singleline_reliable (const char *command,
                                const char *responsePrefix,
                                 ATResponse **pp_outResponse,
                                 int numOfRetransmit);
int at_send_command_reliable (const char *command,
				ATResponse **pp_outResponse,
                              int numOfRetransmit);
static void convertToIpv6Addr(char* formatAddr,char* ipv6Addr);
static int getIpAddress(const char* if_name,char* address,int addressFamily);
static void requestDeactivateDataCall(void *data, size_t datalen, RIL_Token t);


static int clccStateToRILState(int state, RIL_CallState *p_state)

{
    switch(state) {
        case 0: *p_state = RIL_CALL_ACTIVE;   return 0;
        case 1: *p_state = RIL_CALL_HOLDING;  return 0;
        case 2: *p_state = RIL_CALL_DIALING;  return 0;
        case 3: *p_state = RIL_CALL_ALERTING; return 0;
        case 4: *p_state = RIL_CALL_INCOMING; return 0;
        case 5: *p_state = RIL_CALL_WAITING;  return 0;
        default: return -1;
    }
}

/**
 * Note: directly modified line and has *p_call point directly into
 * modified line
 */
static int callFromCLCCLine(char *line, RIL_Call *p_call)
{
	    //[+CLCC: <ccid1>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
	    //  [<CR><LF>+CLCC: <ccid2>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>[,<alpha>[,<priority>[,<CLI validity>]]]]
	    //  [...]]]

        //+CLCC: 1,0,2,0,0,\"+18005551212\",145
        //     index,isMT,state,mode,isMpty(,number,TOA)?

    int err;
    int state;
    int mode;


    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* ccid */
    err = at_tok_nextint(&line, &(p_call->index));
    if (err < 0) goto error;

    /* dir */
    err = at_tok_nextbool(&line, &(p_call->isMT));
    if (err < 0) goto error;

    /* state */
    err = at_tok_nextint(&line, &state);
    if (err < 0) goto error;

    err = clccStateToRILState(state, &(p_call->state));
    if (err < 0) goto error;

    /* mode */
    err = at_tok_nextint(&line, &mode);
    if (err < 0) goto error;

    p_call->isVoice = (mode == 0);

    /* mpty */
    err = at_tok_nextbool(&line, &(p_call->isMpty));
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        /* numbrt */
        err = at_tok_nextstr(&line, &(p_call->number));

        /* tolerate null here */
        if (err < 0) return 0;

        // Some lame implementations return strings
        // like "NOT AVAILABLE" in the CLCC line
        if (p_call->number != NULL
            && 0 == strspn(p_call->number, "+0123456789")
        ) {
            p_call->number = NULL;
        }

        /* type */
        err = at_tok_nextint(&line, &p_call->toa);
        if (err < 0) goto error;
    }

    p_call->uusInfo = NULL;

    return 0;

error:
    RLOGE("invalid CLCC line\n");
    return -1;
}


/** do post-AT+CFUN=1 initialization */
static void onRadioPowerOn()
{
#ifdef USE_TI_COMMANDS
    /*  Must be after CFUN=1 */
    /*  TI specific -- notifications for CPHS things such */
    /*  as CPHS message waiting indicator */

    at_send_command("AT%CPHS=1", NULL);

    /*  TI specific -- enable NITZ unsol notifs */
    at_send_command("AT%CTZV=1", NULL);
#endif
    //since our modem is auto connect , no need to send connect request.
    //connectLTE();
    pollSIMState(NULL);
}

/** do post- SIM ready initialization */
static void onSIMReady()
{
    at_send_command_singleline_reliable("AT+CSMS=1", "+CSMS:", NULL,NUM_OF_RETRANSMIT);
    /*
     * Always send SMS messages directly to the TE
     *
     * mode = 1 // discard when link is reserved (link should never be
     *             reserved)
     * mt = 2   // most messages routed to TE
     * bm = 2   // new cell BM's routed to TE
     * ds = 1   // Status reports routed to TE
     * bfr = 1  // flush buffer
     */
    /*  SMS PDU mode */
    at_send_command("AT+CMGF=0", NULL);

    at_send_command("AT+CNMI=1,2,2,1,1", NULL);
}

static void requestRadioPower(void *data, size_t datalen, RIL_Token t)
{
    int onOff;
    int i;
    int err;
    ATResponse *p_response = NULL;
    PdnTable 	pdnTbl;

    assert (datalen >= sizeof(int *));
    onOff = ((int *)data)[0];

    if (onOff == 0 && sState != RADIO_STATE_OFF) {
        // turn off all Internet / data interfaces
    	err = getPdnTable(&pdnTbl);
		if ( err <0)
		{
			RLOGE("requestRadioPower got error from getPdnTable");
			goto error;
		}
		for (i = 0; i < pdnTbl.numOfEntries; i++) {
			if (ifc_config(IFNAME(pdnTbl.pdn[i].sessionId),IF_STATE_DOWN)<0)
			    	goto error;
		}


    	// power off == flight mode , there is no cfun=0.
       err = at_send_command_reliable("AT+CFUN=4", &p_response,NUM_OF_RETRANSMIT);
       if (err < 0 || p_response->success == 0) goto error;
       setRadioState(RADIO_STATE_OFF);


    } else if (onOff > 0 && (sState == RADIO_STATE_OFF || sState == RADIO_STATE_UNAVAILABLE)) {
        err = at_send_command_reliable("AT+CFUN=1", &p_response,NUM_OF_RETRANSMIT);
        if (err < 0|| p_response->success == 0) {
            // Some stacks return an error when there is no SIM,
            // but they really turn the RF portion on
            // So, if we get an error, let's check to see if it
            // turned on anyway

            if (isRadioOn() != 1) {
                goto error;
            }
        }
        setRadioState(RADIO_STATE_ON);
    }

    at_response_free(p_response);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    return;
error:
    at_response_free(p_response);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestOrSendDataCallList(RIL_Token *t,PdnTableEntry* pdn);

static void onDataCallListChanged(void *param)
{
    RLOGD("onDataCallListChanged" );
    requestOrSendDataCallList(NULL,NULL);
}

static void requestDataCallList(void *data, size_t datalen, RIL_Token t)
{
    requestOrSendDataCallList(&t,NULL);
}



/*
 * modified by altair
 * we will follow the following procedure to support multi PDN
 * (1) Use PDNACT? to get a list of CID that are to be reported back as data call.
 * (2) for each CID from PDNACT do the following
 * (2.1) get state from the PDNACT? respond
 * (2.2) Use AT+CGCONTRDP=<cid> to get additional parameters 
 * (2.3) if a specific Pdn is specified as an input - retrive only that pdn info. 
 */
static void requestOrSendDataCallList(RIL_Token *t,PdnTableEntry* pdn)
{
    ATResponse 	*p_response=NULL;
    ATLine 		*p_cur;
    char 		*out;
	PdnTable 	pdnTbl;
    int         numOfPdns;
    PdnTableEntry *pdnList;
	int 		err;
	char 		*cmd;
    int 		isIpv6Line = 0;
	char *p;
    int dots = 0;
    char *mask = NULL;
    char *dns = NULL;
    int prefixlen = 0;
    char localIpv6address[80];
    char* addr;


    if (!pdn) {
        //Use PDNACT? to get a list of CID that are to be reported back as data call.
        err = getPdnTable(&pdnTbl);
        if ( err <0)
        {
            RLOGE("requestOrSendDataCallList got error from getPdnTable");
            goto error;
        }
        pdnList = pdnTbl.pdn;
        numOfPdns = pdnTbl.numOfEntries;
    }
    else
    {
        pdnList = pdn;
        numOfPdns = 1;
    }

	RIL_Data_Call_Response_v6 *responses =
	        alloca(numOfPdns * sizeof(RIL_Data_Call_Response_v6));

	int i;
	for (i = 0; i < numOfPdns; i++) {
		responses[i].status = -1;
		responses[i].suggestedRetryTime = -1;
		responses[i].cid = pdnList[i].cid;
		responses[i].active = pdnList[i].active;
		responses[i].type = "";
		responses[i].ifname = "";
		responses[i].addresses = "";
		responses[i].dnses = "";
		responses[i].gateways = "";

		isIpv6Line = 0;
		if ( responses[i].active == 0)
			continue; // there is not more data for this PDN

		// +CGCONTRDP: <cid>, <bearer_id>, <apn>[,<source address and subset mask>[, <gw_addr>[, <DNS_prim_addr>[, <DNS_sec_addr>[, <P-CSCF_prim_addr>[, <P-CSCF_sec_addr>]]]]]]

		asprintf(&cmd, "AT+CGCONTRDP=%d", pdnList[i].cid);
		err = at_send_command_multiline(cmd, "+CGCONTRDP:",&p_response);
		free(cmd);
		if (err != 0 || p_response->success == 0) {
			RLOGD("CGCONTRDP returned an error %d %d",err,(p_response != NULL)? p_response->success: 0 );
			goto error;
		}
		p_cur = p_response->p_intermediates;

		// note the respond may be of 2 lines , one for IPv4 and one for IPv6
		for (; p_cur != NULL;p_cur = p_cur->p_next) {
	        char *line = p_cur->line;
	        int cid;

	        err = at_tok_start(&line);
	        if (err < 0)
	            goto error;

	        err = at_tok_nextint(&line, &cid);
	        if (err < 0)
	        {
	            RLOGD("couldn't locate CID In CGCONTRDP answer");
	            goto error;
	        }

	        for (i = 0; i < numOfPdns; i++) {
	            if (responses[i].cid == cid)
	                break;
	        }

	        if (i >= numOfPdns) {
	            /* details for a context we didn't hear about in the last request */
	            RLOGD("couldn't locate the CGDCONTRDP cid # int CGDCONT answer");
	            goto error;
	        }

	        // +CGCONTRDP: <cid>, <bearer_id>, <apn>[,<source address and subnet mask>[, <gw_addr>[, <DNS_prim_addr>[, <DNS_sec_addr>[, <P-CSCF_prim_addr>[, <P-CSCF_sec_addr>]]]]]]
	        if (responses[i].status != -1) // 1st time for thie session ID , the status is -1 and the IP is v4 , 2nd time the staus is 0 and the IP is IPv6
	        {
	            if (isIpv6Line)
	            {
	                RLOGE("can't be more then two lines for CGCONTRDP! %s",line);
	                break;
	            }
	            isIpv6Line = 1;
	            RLOGD("looking at the second line for ipv6: %s ",line);
	            strcpy(responses[i].type, "IPV4V6");
	        }
	        else
	        {
	            // Assume no error
	            responses[i].status = 0;

	            responses[i].type = alloca(strlen("IPV4V6") + 1);
	            strcpy(responses[i].type, "IP");

	            // get ifname from hard codded structure
				if (pdnList[i].sessionId<MAX_NUM_OF_PDN){
					responses[i].ifname = alloca(strlen(ETH_IF_NAME[pdnList[i].sessionId]) + 1);
					strcpy(responses[i].ifname, ETH_IF_NAME[pdnList[i].sessionId]);
				}else{
					RLOGD("pdnTbl.sessionId[%d]<MAX_NUM_OF_PDN",i);
					goto error;
				}
	        }

	        // ignore bearer_id
	        err = at_tok_nextstr(&line, &out);
	        if (err < 0)
	        {
	            RLOGD("couldn't locate bearer_id In CGCONTRDP answer");
	            goto error;
	        }

	        // APN ignored for v5
	        err = at_tok_nextstr(&line, &out);
	        if (err < 0)
	        {
	            RLOGD("couldn't locate APN In CGCONTRDP answer");
	            goto error;
	        }

	        err = at_tok_nextstr(&line, &out);
	        if (err < 0)
	        {
	            RLOGD("couldn't locate source/mask addr In CGCONTRDP answer");
	            goto error;
	        }

	        if (!isIpv6Line)
	        {

				// convert the CGCONTRDP ip result to ipv4 readable address -
				// example 10.170.1.1.255.255.0.0 to 10.170.1.1/16
	        	dots = 0;
	            mask = NULL;
	            prefixlen = 0;
	            for (p = out ; *p != '\0' ;p++) {
	                if (*p == '.')
	                {
	                    dots++;
	                    if (dots == 4)
	                    {
	                        *p = '/';
	                        mask = p+1;
	                    }

	                    if (mask != NULL)
	                    {
	                        if (*(p+1) == '0')
	                        {
	                            break;
	                        }
	                        else
	                        {
	                            prefixlen +=8;
	                        }
	                    }
	                }
	            }

	            sprintf(mask,"%d",prefixlen);

	            responses[i].addresses = alloca(strlen(out) + 1);
	            strcpy(responses[i].addresses, out);
	        }
	        else
	        {
                int retryCount = 0;
	            // altair - since CGCONTRDP for ipv6 doesn't give us the global ip address of the system
	            // (it only gets the local link suffix to be used to get the global address)
	            // we need to get the address from the OS.
	            // it might be that due to timing - the ipv6 address hasn't been resolved yet.
                // we allow 5 sec for it to be resolved.
	            while(retryCount < 5) {
                    err = getIpAddress(ETH_IF_NAME[pdnList[i].sessionId],localIpv6address,AF_INET6);
                    if (err >= 0)
                    {
                        RLOGD("found addresses %s",localIpv6address);
                        asprintf(&addr,"%s %s",responses[i].addresses,localIpv6address);

                        RLOGD("sending addresses %s",addr);
                        responses[i].addresses = alloca(strlen(addr) + 1);
                        strcpy(responses[i].addresses, addr);
                        free(addr);
                        break;
                    }
                    sleep(1);
                    retryCount++;
                    RLOGD("try %d to get global ipv6 address",retryCount);
                }
	        }

	        //extract gateway and dns
	        err = at_tok_nextstr(&line, &out);
	        if (err < 0)
	        {
	            RLOGD("couldn't locate gateway In CGCONTRDP answer");
	            goto error;
	        }

	        // altair - disregarding gateway for ipv6
	        if (!isIpv6Line)
	        {
	            responses[i].gateways = alloca(strlen(out) + 1);
	            strcpy(responses[i].gateways, out);
	        }

	        err = at_tok_nextstr(&line, &out);
	        if (err < 0)
	            //if there are no dns - use google's default dns
	            responses[i].dnses = "8.8.8.8 8.8.4.4";
	        else
	        {
	            if (!isIpv6Line)
	            {
	                dns = out;
	            }
	            else
	            {
	                convertToIpv6Addr(localIpv6address,out);
	                asprintf(&addr,"%s %s",responses[i].dnses,localIpv6address);
	                responses[i].dnses = alloca(strlen(addr) + 1);
	                strcpy(responses[i].dnses, addr);
	                free(addr);
	            }

	            err = at_tok_nextstr(&line, &out);

	            if (!isIpv6Line)
	            {
	                if (!(err < 0))
	                    *(out-1) = ' ';

	                responses[i].dnses = alloca(strlen(dns) + 1);
	                strcpy(responses[i].dnses, dns);
	            }
	            else
	            {
	                if (!(err<0) && strlen(out) > 0)
	                {
	                    convertToIpv6Addr(localIpv6address,out);
	                    asprintf(&addr,"%s %s",responses[i].dnses,localIpv6address);
	                    responses[i].dnses = alloca(strlen(addr) + 1);
	                    strcpy(responses[i].dnses, addr);
	                    free(addr);
	                }
	            }

	        }
		} // for CGCONTRDP respond
	} // for PdnTable
    at_response_free(p_response);
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_SUCCESS, responses,
        		numOfPdns * sizeof(RIL_Data_Call_Response_v6));
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                                  responses,
                                  numOfPdns * sizeof(RIL_Data_Call_Response_v6));
    return;

error:
    if (t != NULL)
        RIL_onRequestComplete(*t, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED,
                                  NULL, 0);

    at_response_free(p_response);
}

static void requestQueryNetworkSelectionMode(
                void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    int response = 0;
    char *line;

    err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);

    if (err < 0) {
        goto error;
    }

    err = at_tok_nextint(&line, &response);

    if (err < 0) {
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
    at_response_free(p_response);
    return;
error:
    at_response_free(p_response);
    RLOGE("requestQueryNetworkSelectionMode must never return error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void sendCallStateChanged(void *param)
{
    RIL_onUnsolicitedResponse (
        RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
        NULL, 0);
}

#ifdef RIL_FEATURE_ENABLE_VOLTE
static void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *p_response;
    ATLine *p_cur;
    int countCalls;
    int countValidCalls;
    RIL_Call *p_calls;
    RIL_Call **pp_calls;
    int i;
    int needRepoll = 0;

#ifdef WORKAROUND_ERRONEOUS_ANSWER
    int prevIncomingOrWaitingLine;

    prevIncomingOrWaitingLine = s_incomingOrWaitingLine;
    s_incomingOrWaitingLine = -1;
#endif /*WORKAROUND_ERRONEOUS_ANSWER*/

    err = at_send_command_multiline ("AT+CLCC", "+CLCC:", &p_response);

    if (err != 0 || p_response->success == 0) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }



    /* count the calls */
    for (countCalls = 0, p_cur = p_response->p_intermediates
            ; p_cur != NULL
            ; p_cur = p_cur->p_next
    ) {
        countCalls++;
    }

    /* yes, there's an array of pointers and then an array of structures */

    pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
    p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
    memset (p_calls, 0, countCalls * sizeof(RIL_Call));

    /* init the pointer array */
    for(i = 0; i < countCalls ; i++) {
        pp_calls[i] = &(p_calls[i]);
    }

    for (countValidCalls = 0, p_cur = p_response->p_intermediates
            ; p_cur != NULL
            ; p_cur = p_cur->p_next
    ) {
        err = callFromCLCCLine(p_cur->line, p_calls + countValidCalls);

        if (err != 0) {
            continue;
        }

#ifdef WORKAROUND_ERRONEOUS_ANSWER
        if (p_calls[countValidCalls].state == RIL_CALL_INCOMING
            || p_calls[countValidCalls].state == RIL_CALL_WAITING
        ) {
            s_incomingOrWaitingLine = p_calls[countValidCalls].index;
        }
#endif /*WORKAROUND_ERRONEOUS_ANSWER*/

        if (p_calls[countValidCalls].state != RIL_CALL_ACTIVE
            && p_calls[countValidCalls].state != RIL_CALL_HOLDING
        ) {
            needRepoll = 1;
        }

        countValidCalls++;
    }

#ifdef WORKAROUND_ERRONEOUS_ANSWER
    // Basically:
    // A call was incoming or waiting
    // Now it's marked as active
    // But we never answered it
    //
    // This is probably a bug, and the call will probably
    // disappear from the call list in the next poll
    if (prevIncomingOrWaitingLine >= 0
            && s_incomingOrWaitingLine < 0
            && s_expectAnswer == 0
    ) {
        for (i = 0; i < countValidCalls ; i++) {

            if (p_calls[i].index == prevIncomingOrWaitingLine
                    && p_calls[i].state == RIL_CALL_ACTIVE
                    && s_repollCallsCount < REPOLL_CALLS_COUNT_MAX
            ) {
                RLOGI(
                    "Hit WORKAROUND_ERRONOUS_ANSWER case."
                    " Repoll count: %d\n", s_repollCallsCount);
                s_repollCallsCount++;
                goto error;
            }
        }
    }

    s_expectAnswer = 0;
    s_repollCallsCount = 0;
#endif /*WORKAROUND_ERRONEOUS_ANSWER*/

    RIL_onRequestComplete(t, RIL_E_SUCCESS, pp_calls,
            countValidCalls * sizeof (RIL_Call *));

    at_response_free(p_response);

#ifdef POLL_CALL_STATE
    if (countValidCalls) {  // We don't seem to get a "NO CARRIER" message from
                            // smd, so we're forced to poll until the call ends.
#else
    if (needRepoll) {
#endif
        RIL_requestTimedCallback (sendCallStateChanged, NULL, &TIMEVAL_CALLSTATEPOLL);
    }

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}
#else //RIL_FEATURE_ENABLE_VOLTE

static void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t)
{
	/* since we do not support voice call , return zero calles */
	RLOGD("requestGetCurrentCalls - this RIL compilation doesn'T support VoLTE");
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL,0);
    return;
}
#endif //RIL_FEATURE_ENABLE_VOLTE

/* CLIR_ORI parameter as defined for +CDU in 3GPP 27.007 */
#define CLIR_ORI_NO_PER_CALL_CHANGE		0
#define CLIR_ORI_RESTRICT 				1
#define CLIR_ORI_ALLOW					2

static void requestDial(void *data, size_t datalen, RIL_Token t)
{
    RIL_Dial *p_dial;
    char *cmd;
    const char *clir;
    int err;
    int clir_ori=0;
    ATResponse *p_response = NULL;

    p_dial = (RIL_Dial *)data;

    switch (p_dial->clir) {
        case 1: clir_ori = CLIR_ORI_ALLOW;              break;  /*invocation*/
        case 2: clir_ori = CLIR_ORI_RESTRICT;           break;  /*suppression*/
        default:
        case 0: clir_ori = CLIR_ORI_NO_PER_CALL_CHANGE; break;   /*subscription default*/
    }
    /* +CDU=<action>[,<URI>[,<client>[,<mpidx>[,<CLIR_OIR>[,<CUG_pointer>]]]]] */
    /* note: current Altair implementation only support first 2 parameter , ignoring clir_ori */
    /* altair todo confirm the adress is of format :tel: <num> */
    asprintf(&cmd, "AT+CDU=1,tel:%s", p_dial->address);


    err = at_send_command_singleline(cmd, "+CDU:", &p_response);
    if (err < 0 || !p_response->success) goto error;

    /* success or failure is ignored by the upper layer here.
       it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    free(cmd);
    at_response_free(p_response);
    return;
error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    free(cmd);
    at_response_free(p_response);
}

static void requestWriteSmsToSim(void *data, size_t datalen, RIL_Token t)
{
    RIL_SMS_WriteArgs *p_args;
    char *cmd;
    int length;
    int err;
    ATResponse *p_response = NULL;

    p_args = (RIL_SMS_WriteArgs *)data;

    length = strlen(p_args->pdu)/2;
    asprintf(&cmd, "AT+CMGW=%d,%d", length, p_args->status);

    err = at_send_command_sms(cmd, p_args->pdu, "+CMGW:", &p_response);

    if (err != 0 || p_response->success == 0) goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    at_response_free(p_response);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}

static void requestHangup(void *data, size_t datalen, RIL_Token t)
{
    int *p_line;

    int ret;
    char *cmd;

    p_line = (int *)data;

    // 3GPP 22.030 6.5.5
    // "Releases a specific active call X"
    // altair todo currently we do not 1x - asprintf(&cmd, "AT+CHLD=1%d", p_line[0]);
    asprintf(&cmd, "AT+CHLD=1");

    ret = at_send_command(cmd, NULL);

    free(cmd);

    /* success or failure is ignored by the upper layer here.
       it will call GET_CURRENT_CALLS and determine success that way */
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}





static void requestSignalStrength(void *data, size_t datalen, RIL_Token t)
{
	int err=-1;
	char *line;
	ATResponse *p_response = NULL;


    RIL_SignalStrength_v6 response_v6;
    //Initialize the unused to -1
    //response_v6.GW_SignalStrength.signalStrength	=-1;
    response_v6.GW_SignalStrength.bitErrorRate		=-1;
    response_v6.CDMA_SignalStrength.dbm				=-1;
    response_v6.CDMA_SignalStrength.ecio			=-1;
    response_v6.EVDO_SignalStrength.dbm				=-1;
    response_v6.EVDO_SignalStrength.ecio			=-1;
    response_v6.EVDO_SignalStrength.signalNoiseRatio=-1;

    // CSQ must always return an answer - return error otherwise
    if (requestSignalStrength_helper(MEAS_CSQ,&response_v6.LTE_SignalStrength.signalStrength) < 0) 
    {
        RLOGE("requestSignalStrength CSQ must never return an invalid measurement");
        goto error;
    }

    // try to get the rest of the measurements if possible.
    requestSignalStrength_helper(MEAS_RSRP, &response_v6.LTE_SignalStrength.rsrp);
    requestSignalStrength_helper(MEAS_RSRQ, &response_v6.LTE_SignalStrength.rsrq);
    requestSignalStrength_helper(MEAS_RSSNR,&response_v6.LTE_SignalStrength.rssnr);
    requestSignalStrength_helper(MEAS_CQI,&response_v6.LTE_SignalStrength.cqi);

    response_v6.GW_SignalStrength.signalStrength = 99; //todo need to update.


    RLOGD("signalStrength=%d,rsrp=%d, rsrq=%d, rssnr=%d, ",response_v6.LTE_SignalStrength.signalStrength,response_v6.LTE_SignalStrength.rsrp,response_v6.LTE_SignalStrength.rsrq,response_v6.LTE_SignalStrength.rssnr);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response_v6, sizeof(RIL_SignalStrength_v6));
    return;

error:
    RLOGE("requestSignalStrength must never return an error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/**
 * networkModePossible. Decides whether the network mode is appropriate for the
 * specified modem
 */
static int networkModePossible(ModemInfo *mdm, int nm)
{
    if ((net2modem[nm] & mdm->supportedTechs) == net2modem[nm]) {
       return 1;
    }
    return 0;
}


 /**
 * requestSetPreferredNetworkType. since only LTE is supported , return success if the requested mode is supported and
 * same as the old mode.
 */
static void requestSetPreferredNetworkType( int request, void *data,
                                            size_t datalen, RIL_Token t )
{
    ATResponse *p_response = NULL;
    char *cmd = NULL;
    int value = *(int *)data;
    int current;
    int err;
    int32_t preferred = net2pmask[value];

    RLOGD("requestSetPreferredNetworkType: current: %x. New: %x", PREFERRED_NETWORK(sMdmInfo), preferred);
    if (!networkModePossible(sMdmInfo, value)) {
    	RLOGD("requested network type %d is not supported", preferred);
        RIL_onRequestComplete(t, RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
        return;
    }
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

/* Altair - the preferred network is LTE */
static void requestGetPreferredNetworkType(int request, void *data,
                                   size_t datalen, RIL_Token t)
{
//NETWORK_MODE_LTE_ONLY       = 11; /* LTE Only mode. */

    int response = 11;

    RLOGD("requestGetPreferredNetworkType returned: %d" , response);

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));

    return;
}



static void requestCdmaPrlVersion(int request, void *data,
                                   size_t datalen, RIL_Token t)
{
    int err;
    char * responseStr;
    ATResponse *p_response = NULL;
    const char *cmd;
    char *line;

    err = at_send_command_singleline("AT+WPRL?", "+WPRL:", &p_response);
    if (err < 0 || !p_response->success) goto error;
    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;
    err = at_tok_nextstr(&line, &responseStr);
    if (err < 0 || !responseStr) goto error;
    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, strlen(responseStr));
    at_response_free(p_response);
    return;
error:
    at_response_free(p_response);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestLteBaseBandVersion(int request, void *data,
                                   size_t datalen, RIL_Token t)
{
    int err;
    char * responseStr;
    ATResponse *p_response = NULL;
    char *line;


    err = at_send_command_singleline("AT+CGMR", "LTEUSB", &p_response);
    if ( (err != 0) || (p_response->p_intermediates == NULL) )
    {
    	err = at_send_command_singleline("AT+CGMR", "ALT", &p_response);
    	if ( (err != 0) || (p_response->p_intermediates == NULL) )
    		goto error;
    }
    line = p_response->p_intermediates->line;
	err = at_tok_nextstr(&line, &responseStr);
	if (err < 0) goto error;
	RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, strlen(responseStr));

    at_response_free(p_response);
	return;

error:
   RLOGE("requestBasebandVersion failed");
   RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
   at_response_free(p_response);
}

static void requestCdmaDeviceIdentity(int request, void *data,
                                        size_t datalen, RIL_Token t)
{
    int err;
    int response[4];
    char * responseStr[4];
    ATResponse *p_response = NULL;
    const char *cmd;
    const char *prefix;
    char *line, *p;
    int commas;
    int skip;
    int count = 4;

    // Fixed values. TODO: Query modem
    responseStr[0] = "----";
    responseStr[1] = "----";
    responseStr[2] = "77777777";

    err = at_send_command_numeric("AT+CGSN", &p_response);
    if (err < 0 || p_response->success == 0) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    } else {
        responseStr[3] = p_response->p_intermediates->line;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, count*sizeof(char*));
    at_response_free(p_response);

    return;
error:
    RLOGE("requestCdmaDeviceIdentity must never return an error when radio is on");
    at_response_free(p_response);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestCdmaGetSubscriptionSource(int request, void *data,
                                        size_t datalen, RIL_Token t)
{
    int err;
    int *ss = (int *)data;
    ATResponse *p_response = NULL;
    char *cmd = NULL;
    char *line = NULL;
    int response;

    asprintf(&cmd, "AT+CCSS?");
    if (!cmd) goto error;

    err = at_send_command_singleline(cmd, "+CCSS:", &p_response);
    if (err < 0 || !p_response->success)
        goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &response);
    free(cmd);
    cmd = NULL;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));

    return;
error:
    free(cmd);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestCdmaSetSubscriptionSource(int request, void *data,
                                        size_t datalen, RIL_Token t)
{
    int err;
    int *ss = (int *)data;
    ATResponse *p_response = NULL;
    char *cmd = NULL;

    if (!ss || !datalen) {
        RLOGE("RIL_REQUEST_CDMA_SET_SUBSCRIPTION without data!");
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }
    asprintf(&cmd, "AT+CCSS=%d", ss[0]);
    if (!cmd) goto error;

    err = at_send_command(cmd, &p_response);
    if (err < 0 || !p_response->success)
        goto error;
    free(cmd);
    cmd = NULL;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

    RIL_onUnsolicitedResponse(RIL_UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED, ss, sizeof(ss[0]));

    return;
error:
    free(cmd);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestCdmaSubscription(int request, void *data,
                                        size_t datalen, RIL_Token t)
{
    int err;
    int response[5];
    char * responseStr[5];
    ATResponse *p_response = NULL;
    const char *cmd;
    const char *prefix;
    char *line, *p;
    int commas;
    int skip;
    int count = 5;

    // Fixed values. TODO: Query modem
    responseStr[0] = "8587777777"; // MDN
    responseStr[1] = "1"; // SID
    responseStr[2] = "1"; // NID
    responseStr[3] = "8587777777"; // MIN
    responseStr[4] = "1"; // PRL Version
    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, count*sizeof(char*));

    return;
error:
    RLOGE("requestRegistrationState must never return an error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestCdmaGetRoamingPreference(int request, void *data,
                                                 size_t datalen, RIL_Token t)
{
    int roaming_pref = -1;
    ATResponse *p_response = NULL;
    char *line;
    int res;

    res = at_send_command_singleline("AT+WRMP?", "+WRMP:", &p_response);
    if (res < 0 || !p_response->success) {
        goto error;
    }
    line = p_response->p_intermediates->line;

    res = at_tok_start(&line);
    if (res < 0) goto error;

    res = at_tok_nextint(&line, &roaming_pref);
    if (res < 0) goto error;

     RIL_onRequestComplete(t, RIL_E_SUCCESS, &roaming_pref, sizeof(roaming_pref));
    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static void requestCdmaSetRoamingPreference(int request, void *data,
                                                 size_t datalen, RIL_Token t)
{
    int *pref = (int *)data;
    ATResponse *p_response = NULL;
    char *line;
    int res;
    char *cmd = NULL;

    asprintf(&cmd, "AT+WRMP=%d", *pref);
    if (cmd == NULL) goto error;

    res = at_send_command(cmd, &p_response);
    if (res < 0 || !p_response->success)
        goto error;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    free(cmd);
    return;
error:
    free(cmd);
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

static int parseRegistrationState(char *str, int *type, int *items, int **response)
{
    int err;
    char *line = str, *p;
    int *resp = NULL;
    int skip;
    int count = 3;
    int commas;

    RLOGD("parseRegistrationState. Parsing: %s",str);
    err = at_tok_start(&line);
    if (err < 0) goto error;

    /* Ok you have to be careful here
     * The solicited version of the CREG response is
     * +CREG: n, stat, [lac, cid]
     * and the unsolicited version is
     * +CREG: stat, [lac, cid]
     * The <n> parameter is basically "is unsolicited creg on?"
     * which it should always be
     *
     * Now we should normally get the solicited version here,
     * but the unsolicited version could have snuck in
     * so we have to handle both
     *
     * Also since the LAC and CID are only reported when registered,
     * we can have 1, 2, 3, or 4 arguments here
     *
     * finally, a +CGREG: answer may have a fifth value that corresponds
     * to the network type, as in;
     *
     *   +CGREG: n, stat [,lac, cid [,networkType]]
     */

    /* count number of commas */
    commas = 0;
    for (p = line ; *p != '\0' ;p++) {
        if (*p == ',') commas++;
    }

    resp = (int *)calloc(commas + 1, sizeof(int));
    if (!resp) goto error;
    switch (commas) {
        case 0: /* +CREG: <stat> */
            err = at_tok_nextint(&line, &resp[0]);
            if (err < 0) goto error;
            resp[1] = -1;
            resp[2] = -1;
        break;

        case 1: /* +CREG: <n>, <stat> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &resp[0]);
            if (err < 0) goto error;
            resp[1] = -1;
            resp[2] = -1;
            if (err < 0) goto error;
        break;

        case 2: /* +CREG: <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &resp[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[2]);
            if (err < 0) goto error;
        break;
        case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &resp[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[2]);
            if (err < 0) goto error;
        break;
        /* special case for CGREG, there is a fourth parameter
         * that is the network type (unknown/gprs/edge/umts)
         */
        case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
            err = at_tok_nextint(&line, &skip);
            if (err < 0) goto error;
            err = at_tok_nextint(&line, &resp[0]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[1]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[2]);
            if (err < 0) goto error;
            err = at_tok_nexthexint(&line, &resp[3]);
            if (err < 0) goto error;
            count = 4;
        break;
        default:
            goto error;
    }
    s_lac = resp[1];
    s_cid = resp[2];
    if (response)
        *response = resp;
    if (items)
        *items = commas + 1;
    if (type)
        *type = techFromModemType(TECH(sMdmInfo));
    return 0;
error:
    free(resp);
    return -1;
}

#define REG_STATE_LEN 15
#define REG_DATA_STATE_LEN 6
#define CREG_STATE__SEARCHING     		2
#define CREG_STATE__REGISTERED_HOME_NET		1
#define CREG_STATE__REGISTERED_ROAMING		5
#define CREG_ACT__EUTRAN             		7

static void requestRegistrationState(int request, void *data,
                                        size_t datalen, RIL_Token t)
{
    int err;
    int *registration;
    char **responseStr = NULL;
    ATResponse *p_response = NULL;
    const char *cmd;
    const char *prefix;
    char *line;
    int i = 0, j, numElements = 0;
    int count = 3;
    int type, startfrom;

    RLOGD("requestRegistrationState");
    if (request == RIL_REQUEST_VOICE_REGISTRATION_STATE) {
        cmd = "AT+CEREG?";
        prefix = "+CEREG:";
        numElements = 6;
    } else if (request == RIL_REQUEST_DATA_REGISTRATION_STATE) {
        cmd = "AT+CEREG?";
        prefix = "+CEREG:";
        numElements = REG_DATA_STATE_LEN;
    } else {
        assert(0);
        goto error;
    }

    err = at_send_command_singleline(cmd, prefix, &p_response);

    if (err != 0) goto error;

    line = p_response->p_intermediates->line;

    if (parseRegistrationState(line, &type, &count, &registration)) goto error;

    // for LTEonCDMA assume 3gpp2 ( numElements = 14 )
    RLOGD("type=%d,is3gpp2(type) =%d",type,is3gpp2(type));
    if ((is3gpp2(type) == 1) && (request == RIL_REQUEST_VOICE_REGISTRATION_STATE))
    	numElements = 14;


    responseStr = malloc(numElements * sizeof(char *));
    if (!responseStr) goto error;
    memset(responseStr, 0, numElements * sizeof(char *));
    /**
     * The first '4' bytes for both registration states remain the same.
     * But if the request is 'DATA_REGISTRATION_STATE',
     * the 5th and 6th byte(s) are optional.
     */
    if (is3gpp2(type) == 1) {
        RLOGD("registration state type: 3GPP2");
        // TODO: Query modem
        startfrom = 3;
        if(request == RIL_REQUEST_VOICE_REGISTRATION_STATE) {
        	startfrom=numElements; //some of the parameters are null for 3gpp2
        	/* for minimal CDMA operation the respond length for voice must be >=14 ,
        	 * but only the status ( responseStr[0]) is needed , all other parameter should be NULL
        	 */
        	asprintf(&responseStr[3], "%d",RADIO_TECH_LTE);
        	/*
            asprintf(&responseStr[4], "1");     // BSID
            asprintf(&responseStr[5], "123");   // Latitude
            asprintf(&responseStr[6], "222");   // Longitude
            asprintf(&responseStr[7], "0");     // CSS Indicator
            asprintf(&responseStr[8], "4");     // SID
            asprintf(&responseStr[9], "65535"); // NID
            asprintf(&responseStr[10], "0");    // Roaming indicator
            asprintf(&responseStr[11], "1");    // System is in PRL
            asprintf(&responseStr[12], "0");    // Default Roaming indicator
            asprintf(&responseStr[13], "0");    // Reason for denial
            asprintf(&responseStr[14], "0");    // Primary Scrambling Code of Current cell
            */
      } else if (request == RIL_REQUEST_DATA_REGISTRATION_STATE) {
    	    asprintf(&responseStr[3], "%d",RADIO_TECH_LTE);
      }
    } else { // type == RADIO_TECH_3GPP
        RLOGD("registration state type: 3GPP");
        startfrom = 0;
        if (registration[1] != -1)
        	asprintf(&responseStr[1], "%x", registration[1]);
        if (registration[2] != -1)
        	asprintf(&responseStr[2], "%x", registration[2]);
        asprintf(&responseStr[3], "%d",RADIO_TECH_LTE);
    }
    asprintf(&responseStr[0], "%d", registration[0]);

    /**
     * Optional bytes for DATA_REGISTRATION_STATE request
     * 4th byte : Registration denial code
     * 5th byte : The max. number of simultaneous Data Calls
     */
    if(request == RIL_REQUEST_DATA_REGISTRATION_STATE) {
        // asprintf(&responseStr[4], "3");
    	PdnTable pdnTbl;
    	if ( (getPdnTable(&pdnTbl)>=0) && (pdnTbl.numOfEntries>0) && (pdnTbl.numOfEntries<MAX_NUM_OF_PDN) )
        	asprintf(&responseStr[5], "%d",pdnTbl.numOfEntries);
    	else{
    		asprintf(&responseStr[5], "1");
    		RLOGD("only 1 simultaneous data call will be acepted");
    	}
    }

    for (j = startfrom; j < numElements; j++) {
        if (!responseStr[i]) goto error;
    }
    free(registration);
    registration = NULL;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, numElements*sizeof(responseStr));
    for (j = 0; j < numElements; j++ ) {
        free(responseStr[j]);
        responseStr[j] = NULL;
    }
    free(responseStr);
    responseStr = NULL;
    at_response_free(p_response);

    return;
error:
    if (responseStr) {
        for (j = 0; j < numElements; j++) {
            free(responseStr[j]);
            responseStr[j] = NULL;
        }
        free(responseStr);
        responseStr = NULL;
    }
    RLOGE("requestRegistrationState must never return an error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}

static void requestOperator(void *data, size_t datalen, RIL_Token t)
{

	int err=0;
	int i=0;
	int skip;
	ATLine *p_cur;
	char *response[3];
	char *line;
	ATResponse *p_response = NULL;

	memset(response, 0, sizeof(response));

	/* we execute  3 requests and expect something like the following:
	 * +COPS: 0,0,"T - Mobile"
	 * +COPS: 0,1,"TMO"
	 * +COPS: 0,2,"310170"
	 */
	char* copsCmd[]={"AT+COPS=3,0","AT+COPS=3,1","AT+COPS=3,2"};


	while ((err==0) && (i<3)){

		/* select the mode to display the operator  */
		err = at_send_command(copsCmd[i], NULL);
		if (err != 0) goto error;

		err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);
		if (err != 0) goto error;

		line = p_response->p_intermediates->line;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &skip);
		if (err < 0) goto error;

		// If we're unregistered, we may just get
		// a "+COPS: 0" response
		if (!at_tok_hasmore(&line)) {
			response[i] = NULL;
		} else {
				err = at_tok_nextint(&line, &skip);
				if (err < 0) goto error;

			   // a "+COPS: 0, n" response is also possible
				if (!at_tok_hasmore(&line)) {
					response[i] = NULL;
				} else {
						err = at_tok_nextstr(&line, &(response[i]));
						if (err < 0) goto error;
				}
		}
		err=0;
		i++;
	}//while

    if ((i != 3) || (err != 0)) {
        /* expect 3 lines exactly */
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
    at_response_free(p_response);

    return;
error:
    RLOGE("requestOperator must not return error when radio is on");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}

/* currently android support 3 sms send requests:
 *  RIL_REQUEST_SEND_SMS - 3gpp text sms
 *  RIL_REQUEST_CDMA_SEND_SMS - for legacy android 4.3 this is used for 3gpp2 ims
 *  RIL_REQUEST_IMS_SEND_SMS - new support for ims sms for both 3gpp and 3gpp2 ( introduced in 4.4)
 */

static void sendCdmaFormatedSMS(RIL_CDMA_SMS_Message* rcsm, RIL_Token t)
{
    static U8 buffer[400];
    static char hexchar[800];
    char  *hexPtr;
    U32 bufferLen;
    U32 i;

    char *cmd1, *cmd2;
    int msgRef;
    char *line;
    ATResponse *p_response = NULL;

    int err = 1; // Set to go to error:
    RIL_SMS_Response response;
    SMS3GPP2TransportLayer_t sms3gpp2;

    memset(&sms3gpp2,0,sizeof(sms3gpp2));



    // verify data content to test marshalling/unmarshalling:

    RLOGD("TeleserviceID=%d, bIsServicePresent=%d, \
            uServicecategory=%d, sAddress.digit_mode=%d, \
            sAddress.Number_mode=%d, sAddress.number_type=%d,rcsm->sAddress.number_of_digits=%d, bearerDataLen=%d",
            rcsm->uTeleserviceID,  rcsm->bIsServicePresent,
            rcsm->uServicecategory,rcsm->sAddress.digit_mode,
            rcsm->sAddress.number_mode,rcsm->sAddress.number_type,
            rcsm->sAddress.number_of_digits,
            rcsm->uBearerDataLen);

    sms3gpp2.msgType = SMS3GPP2TransMsgType_PointToPoint;
    sms3gpp2.teleserviceIndetifier = rcsm->uTeleserviceID;
    sms3gpp2.destinationAddress.digitMode = rcsm->sAddress.digit_mode;
    sms3gpp2.destinationAddress.numberMode = rcsm->sAddress.number_mode;
    sms3gpp2.destinationAddress.numOfFields = rcsm->sAddress.number_of_digits;
    memcpy (sms3gpp2.destinationAddress.address,rcsm->sAddress.digits,sms3gpp2.destinationAddress.numOfFields);

    sms3gpp2.bearerDataLen = rcsm->uBearerDataLen;
    memcpy(sms3gpp2.bearerData,rcsm->aBearerData,rcsm->uBearerDataLen);
    sms3gpp2.optinalFieldsBitmap |= SMS3GPP2TransOpt_BearerDataOption;
    err = SMS3GPP2_TransportLayer_Serialize(&sms3gpp2,buffer,&bufferLen);

    if (err != 0) goto error;

    hexPtr = hexchar;
    
    for(i =0;i < bufferLen; i++)
    {
        
        hexPtr += sprintf(hexPtr,"%02X",buffer[i]);
    }
    *hexPtr = '\0';

    RLOGD("converting buffer len %d to hex string %s",bufferLen,hexchar);

    // altair - create the SMS send command in PDU mode
    asprintf(&cmd1, "AT+CMGS=%d", bufferLen);
    asprintf(&cmd2, "%s", hexchar);

    err = at_send_command_sms(cmd1, cmd2, "+CMGS:", &p_response);

    if (err < 0 || p_response->success == 0) goto error;

    memset(&response, 0, sizeof(response));

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &msgRef);
    if (err < 0) goto error;

    response.messageRef = msgRef;
    response.ackPDU = "";
    response.errorCode = -1;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));
    return;

error:
    RLOGE("failed to send CDMA sms");
    at_response_free(p_response);
    // Cdma Send SMS will always cause send retry error.
    RIL_onRequestComplete(t, RIL_E_SMS_SEND_FAIL_RETRY, NULL, 0);
}

static void requestSendSMS(void *data, size_t datalen, RIL_Token t)
{
    int err;
    const char *smsc;
    const char *pdu;
    int tpLayerLength;
    char *cmd1, *cmd2;

    char *line;
    int msgRef;

    RIL_SMS_Response response;
    ATResponse *p_response = NULL;

    smsc = ((const char **)data)[0];
    pdu = ((const char **)data)[1];

    tpLayerLength = strlen(pdu)/2;

    // "NULL for default SMSC"
    if (smsc == NULL) {
        smsc= "00";
    }

    asprintf(&cmd1, "AT+CMGS=%d", tpLayerLength);
    asprintf(&cmd2, "%s%s", smsc, pdu);

    err = at_send_command_sms(cmd1, cmd2, "+CMGS:", &p_response);

    if (err != 0 || p_response->success == 0) goto error;

    memset(&response, 0, sizeof(response));

// altair - fill messageRef 
    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &msgRef);
    if (err < 0) goto error;

    response.messageRef = msgRef;
    response.ackPDU = "";
    response.errorCode = -1;

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));
    at_response_free(p_response);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}




static void requestCdmaSendSMS(void *data, size_t datalen, RIL_Token t)
{
	RIL_CDMA_SMS_Message* rcsm;
	RLOGD("requestCdmaSendSMS datalen=%d, sizeof(RIL_CDMA_SMS_Message)=%d",
	            datalen, sizeof(RIL_CDMA_SMS_Message));
	rcsm = (RIL_CDMA_SMS_Message*)data;
	sendCdmaFormatedSMS(rcsm,t);
}


#if (RIL_VERSION_4_4_AND_ABOVE == 1)
static void RequestImsSendSms(void* data, size_t datalen, RIL_Token t)
{
	RIL_IMS_SMS_Message* rism = (RIL_IMS_SMS_Message*)data;


	RIL_CDMA_SMS_Message* rcsm;
	RLOGD("RequestImsSendSms datalen=%d, sizeof(RIL_IMS_SMS_Message)=%d, tech=%d, retry=%d, msgRef=%d ",
	            datalen, sizeof(RIL_IMS_SMS_Message),rism->tech,rism->retry,rism->messageRef );

	if (rism->tech != RADIO_TECH_3GPP2)
	{
		RLOGD("RequestImsSendSms only 3gpp2 tech is supported");
		goto error;
	}


	rcsm = (RIL_CDMA_SMS_Message*)rism->message.cdmaMessage;
	sendCdmaFormatedSMS(rcsm,t);
    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0); //do not try other technologies available
}

#endif

// - altair - adding support to CDMA (3gpp2) SMS - start
static void notifynewCdmaSMS(const char* sms_pdu)
{
    static U8 buffer[400];
    int i;

    const char *curHex;
    char hexTemp[3];

    int smsHexLen;

    int err = 1; // Set to go to error:
    RIL_SMS_Response response;
    RIL_CDMA_SMS_Message* rcsm;
    SMS3GPP2TransportLayer_t *sms3gpp2 = NULL;

    smsHexLen = strlen(sms_pdu);

    RLOGD("notifynewCdmaSMS sms_pdu length=%d",smsHexLen);

    rcsm = alloca(sizeof(RIL_CDMA_SMS_Message));
    memset(rcsm,0,sizeof(RIL_CDMA_SMS_Message));

    if (smsHexLen % 2 != 0)
    {
        RLOGE("notifynewCdmaSMS bad length");
        goto error;
    }

    curHex = sms_pdu;
    hexTemp[2] = '\0';
    for (i = 0; i< smsHexLen/2; i++)
    {
        hexTemp[0] = curHex[0];
        hexTemp[1] = curHex[1];

        buffer[i] = (U8)strtoul(hexTemp,NULL,16);

        curHex+=2;
    }
  
    sms3gpp2 = SMS3GPP2_TransportLayer_Deserialize(buffer,smsHexLen/2);

    if (sms3gpp2 == NULL)
    {
        RLOGE("notifynewCdmaSMS failed to deserialize the sms data");
        goto error;
    }

    rcsm->uTeleserviceID = sms3gpp2->teleserviceIndetifier;
    rcsm->sAddress.digit_mode = sms3gpp2->originatingAddress.digitMode;
    rcsm->sAddress.number_mode = sms3gpp2->originatingAddress.numberMode;
    rcsm->sAddress.number_of_digits = sms3gpp2->originatingAddress.numOfFields;
    memcpy (rcsm->sAddress.digits,sms3gpp2->originatingAddress.address,sms3gpp2->originatingAddress.numOfFields);

    rcsm->uBearerDataLen = sms3gpp2->bearerDataLen;
    memcpy(rcsm->aBearerData,sms3gpp2->bearerData,rcsm->uBearerDataLen);

    RLOGD("TeleserviceID=%d, bIsServicePresent=%d, \
            uServicecategory=%d, sAddress.digit_mode=%d, \
            sAddress.Number_mode=%d, sAddress.number_type=%d, sAddress.number_of_digits = %d",
            rcsm->uTeleserviceID,  rcsm->bIsServicePresent,
            rcsm->uServicecategory,rcsm->sAddress.digit_mode,
            rcsm->sAddress.number_mode,rcsm->sAddress.number_type,
            rcsm->sAddress.number_of_digits);

    SMS3GPP2_TransportLayer_Free(sms3gpp2);
    RIL_onUnsolicitedResponse (RIL_UNSOL_RESPONSE_CDMA_NEW_SMS,
                               rcsm, sizeof(RIL_CDMA_SMS_Message));
    return;

error:
    free(rcsm);
    if (sms3gpp2) free(sms3gpp2);
    return;
}
static void requestSetupDataCall(void *data, size_t datalen, RIL_Token t)
{
    char *apn;
    char *cmd;
    int err;
    ATResponse *p_response = NULL;

    apn = ((char **)data)[2];

#ifdef USE_TI_COMMANDS
    // Config for multislot class 10 (probably default anyway eh?)
    err = at_send_command("AT%CPRIM=\"GMM\",\"CONFIG MULTISLOT_CLASS=<10>\"",
                        NULL);

    err = at_send_command("AT%DATA=2,\"UART\",1,,\"SER\",\"UART\",0", NULL);
#endif /* USE_TI_COMMANDS */

    int fd, qmistatus;
    size_t cur = 0;
    size_t len;
    ssize_t written, rlen;
    char status[32] = {0};
    int retry = 10;
    const char *pdp_type;
    PdnTableEntry pdn;

    RLOGD("requesting data connection to APN '%s'", apn);

#ifdef altair_remove //since we are not in QMI emulation mode , remove this code to save some open transaction
    fd = open ("/dev/qmi", O_RDWR);
    if (fd >= 0) { /* the device doesn't exist on the emulator */

        RLOGD("opened the qmi device\n");
        asprintf(&cmd, "up:%s", apn);
        len = strlen(cmd);

        while (cur < len) {
            do {
                written = write (fd, cmd + cur, len - cur);
            } while (written < 0 && errno == EINTR);

            if (written < 0) {
                RLOGE("### ERROR writing to /dev/qmi");
                close(fd);
                goto error;
            }

            cur += written;
        }

        // wait for interface to come online

        do {
            sleep(1);
            do {
                rlen = read(fd, status, 31);
            } while (rlen < 0 && errno == EINTR);

            if (rlen < 0) {
                RLOGE("### ERROR reading from /dev/qmi");
                close(fd);
                goto error;
            } else {
                status[rlen] = '\0';
                RLOGD("### status: %s", status);
            }
        } while (strncmp(status, "STATE=up", 8) && strcmp(status, "online") && --retry);

        close(fd);

        if (retry == 0) {
            RLOGE("### Failed to get data connection up\n");
            goto error;
        }

        qmistatus = system("netcfg rmnet0 dhcp");

        RLOGD("netcfg rmnet0 dhcp: status %d\n", qmistatus);

        if (qmistatus < 0) goto error;

    } else
#endif
    {

        if (datalen > 6 * sizeof(char *)) {
            pdp_type = ((const char **)data)[6];
        } else {
            pdp_type = "IP";
        }

        // find session for this APN , or updae the internet session
        
        if (getPdnByApnName(apn,&pdn) == -1)
        {
            // if the APN was not found use %APNN
            asprintf(&cmd, "AT%%APNN=\"%s\"", apn);
            //FIXME check for error here
            err = at_send_command(cmd, NULL);
            free(cmd);
            
            // now once we've set the apn - try to retrive again
            if (getPdnByApnName(apn,&pdn) == -1)
                goto error;
        }
        err= activateDataSessionPdn(pdn.sessionId);
		if (err < 0)
		{
			goto error;
		}

        //now the pdn is active
        pdn.active = 1;

    }

    requestOrSendDataCallList(&t,&pdn);

    at_response_free(p_response);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);

}

static void requestSMSAcknowledge(void *data, size_t datalen, RIL_Token t)
{
    int ackSuccess;
    int err;

    ackSuccess = ((int *)data)[0];

    if (ackSuccess == 1) {
        err = at_send_command("AT+CNMA=1", NULL);
    } else if (ackSuccess == 0)  {
        err = at_send_command("AT+CNMA=2", NULL);
    } else {
        RLOGE("unsupported arg to RIL_REQUEST_SMS_ACKNOWLEDGE\n");
        goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}

static void  requestSIM_IO(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *p_response = NULL;
    RIL_SIM_IO_Response sr;
    int err;
    char *cmd = NULL;
    RIL_SIM_IO_v6 *p_args;
    char *line;

    memset(&sr, 0, sizeof(sr));

    p_args = (RIL_SIM_IO_v6 *)data;

    /* FIXME handle pin2 */
    /* todo Altair - support ef_mailbox_cphs */


    if (p_args->data == NULL) {
        asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d",
                    p_args->command, p_args->fileid,
                    p_args->p1, p_args->p2, p_args->p3);
    } else {
        asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d,%s",
                    p_args->command, p_args->fileid,
                    p_args->p1, p_args->p2, p_args->p3, p_args->data);
    }

    err = at_send_command_singleline(cmd, "+CRSM:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw1));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw2));
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        err = at_tok_nextstr(&line, &(sr.simResponse));
        if (err < 0) goto error;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, &sr, sizeof(sr));
    at_response_free(p_response);
    free(cmd);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
    free(cmd);

}

static void  requestEnterSimPin(void*  data, size_t  datalen, RIL_Token  t)
{
    ATResponse   *p_response = NULL;
    int           err;
    char*         cmd = NULL;
    const char**  strings = (const char**)data;;

    if (( datalen == sizeof(char*) ) || (strings[1]== NULL)) {
        asprintf(&cmd, "AT+CPIN=\"%s\"", strings[0]);
    } else if ( datalen == 2*sizeof(char*) ) {
        asprintf(&cmd, "AT+CPIN=\"%s\",\"%s\"", strings[0], strings[1]);
    } else
        goto error;

    err = at_send_command_singleline(cmd, "+CPIN:", &p_response);
    free(cmd);

    if (err < 0 || p_response->success == 0) {
error:
        RIL_onRequestComplete(t, RIL_E_PASSWORD_INCORRECT, NULL, 0);
    } else {
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    }
    at_response_free(p_response);
}


static void  requestSendUSSD(void *data, size_t datalen, RIL_Token t)
{
    const char *ussdRequest;

    ussdRequest = (char *)(data);


    RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);

// @@@ TODO

}

static void requestExitEmergencyMode(void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;

    err = at_send_command("AT+WSOS=0", &p_response);

    if (err < 0 || p_response->success == 0) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}

// TODO: Use all radio types
static int techFromModemType(int mdmtype)
{
    int ret = -1;
    switch (1 << mdmtype) {
        case MDM_CDMA:
            ret = RADIO_TECH_1xRTT;
            break;
        case MDM_EVDO:
            ret = RADIO_TECH_EVDO_A;
            break;
        case MDM_GSM:
            ret = RADIO_TECH_GPRS;
            break;
        case MDM_WCDMA:
            ret = RADIO_TECH_HSPA;
            break;
        case MDM_LTE:
            ret = RADIO_TECH_LTE;
            break;
    }
    return ret;
}
#if (RIL_VERSION_4_3_AND_ABOVE == 1)
static void requestGetCellInfoList(void *data, size_t datalen, RIL_Token t)
{
    uint64_t curTime = ril_nano_time();
    RIL_CellInfo ci[1] =
    {
        { // ci[0]
            1, // cellInfoType
            1, // registered
            curTime - 1000, // Fake some time in the past
            { // union CellInfo
                {  // RIL_CellInfoGsm gsm
                    {  // gsm.cellIdneityGsm
                        s_mcc, // mcc
                        s_mnc, // mnc
                        s_lac, // lac
                        s_cid, // cid
                        0  // psc
                    },
                    {  // gsm.signalStrengthGsm
                        10, // signalStrength
                        0  // bitErrorRate
                    }
                }
            }
        }
    };

    RIL_onRequestComplete(t, RIL_E_SUCCESS, ci, sizeof(ci));
}


static void requestSetCellInfoListRate(void *data, size_t datalen, RIL_Token t)
{
    // For now we'll save the rate but no RIL_UNSOL_CELL_INFO_LIST messages
    // will be sent.
    assert (datalen == sizeof(int));
    s_cell_info_rate_ms = ((int *)data)[0];

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
#endif


#if (RIL_VERSION_4_4_AND_ABOVE == 1)
static void requestImsRegistrationState(void *data, size_t datalen, RIL_Token t)
{

     char *response[2];



	 response[0]=1; 					// IMS is connected
	 response[1]=RADIO_TECH_3GPP2;  	// todo should get this based on operator

	 RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
}

#endif //RIL_VERSION_4_4_AND_ABOVE



/*** Callback methods from the RIL library to us ***/

/**
 * Call from RIL to us to make a RIL_REQUEST
 *
 * Must be completed with a call to RIL_onRequestComplete()
 *
 * RIL_onRequestComplete() may be called from any thread, before or after
 * this function returns.
 *
 * Will always be called from the same thread, so returning here implies
 * that the radio is ready to process another command (whether or not
 * the previous command has completed).
 */
static void
onRequest (int request, void *data, size_t datalen, RIL_Token t)
{
    ATResponse *p_response=NULL;
    int err;

    RLOGD("onRequest: %s", requestToString(request));

    /* Ignore all requests except RIL_REQUEST_GET_SIM_STATUS
     * when RADIO_STATE_UNAVAILABLE.
     */
    if (sState == RADIO_STATE_UNAVAILABLE
        && request != RIL_REQUEST_GET_SIM_STATUS
    ) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;
    }

    /* Ignore all non-power requests when RADIO_STATE_OFF
     * (except RIL_REQUEST_GET_SIM_STATUS)
     */
    if (sState == RADIO_STATE_OFF
        && !(request == RIL_REQUEST_RADIO_POWER
			|| request == RIL_REQUEST_GET_SIM_STATUS
			|| request == RIL_REQUEST_BASEBAND_VERSION
			|| request == RIL_REQUEST_GET_IMEI
			|| request == RIL_REQUEST_GET_IMEISV
			|| request == RIL_REQUEST_SCREEN_STATE)
    ) {
        RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        return;
    }

    switch (request) {
    	case RIL_REQUEST_SCREEN_STATE:
    		RIL_onRequestComplete(t, RIL_E_SUCCESS,NULL, 0);
    		break;
        case RIL_REQUEST_GET_SIM_STATUS: {
            RIL_CardStatus_v6 *p_card_status;
            char *p_buffer;
            int buffer_size;

            int result = getCardStatus(&p_card_status);
            if (result == RIL_E_SUCCESS) {
                p_buffer = (char *)p_card_status;
                buffer_size = sizeof(*p_card_status);
            } else {
                p_buffer = NULL;
                buffer_size = 0;
            }
            RIL_onRequestComplete(t, result, p_buffer, buffer_size);
            freeCardStatus(p_card_status);
            break;
        }
        case RIL_REQUEST_GET_CURRENT_CALLS:
            requestGetCurrentCalls(data, datalen, t);
            break;
        case RIL_REQUEST_DIAL:
            requestDial(data, datalen, t);
            break;
        case RIL_REQUEST_HANGUP:
            requestHangup(data, datalen, t);
            break;
        case RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND:
            // 3GPP 22.030 6.5.5
            // "Releases all held calls or sets User Determined User Busy
            //  (UDUB) for a waiting call."
            at_send_command("AT+CHLD=0", NULL);

            /* success or failure is ignored by the upper layer here.
               it will call GET_CURRENT_CALLS and determine success that way */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;
        case RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND:
            // 3GPP 22.030 6.5.5
            // "Releases all active calls (if any exist) and accepts
            //  the other (held or waiting) call."
            at_send_command("AT+CHLD=1", NULL);

            /* success or failure is ignored by the upper layer here.
               it will call GET_CURRENT_CALLS and determine success that way */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;
        case RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE:
            // 3GPP 22.030 6.5.5
            // "Places all active calls (if any exist) on hold and accepts
            //  the other (held or waiting) call."
            at_send_command("AT+CHLD=2", NULL);

#ifdef WORKAROUND_ERRONEOUS_ANSWER
            s_expectAnswer = 1;
#endif /* WORKAROUND_ERRONEOUS_ANSWER */

            /* success or failure is ignored by the upper layer here.
               it will call GET_CURRENT_CALLS and determine success that way */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;
        case RIL_REQUEST_ANSWER:
            at_send_command("ATA", NULL);

#ifdef WORKAROUND_ERRONEOUS_ANSWER
            s_expectAnswer = 1;
#endif /* WORKAROUND_ERRONEOUS_ANSWER */

            /* success or failure is ignored by the upper layer here.
               it will call GET_CURRENT_CALLS and determine success that way */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;
        case RIL_REQUEST_CONFERENCE:
            // 3GPP 22.030 6.5.5
            // "Adds a held call to the conversation"
            at_send_command("AT+CHLD=3", NULL);

            /* success or failure is ignored by the upper layer here.
               it will call GET_CURRENT_CALLS and determine success that way */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;
        case RIL_REQUEST_UDUB:
            /* user determined user busy */
            /* sometimes used: ATH */
            at_send_command("ATH", NULL);

            /* success or failure is ignored by the upper layer here.
               it will call GET_CURRENT_CALLS and determine success that way */
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;

        case RIL_REQUEST_SEPARATE_CONNECTION:
            {
                char  cmd[12];
                int   party = ((int*)data)[0];

                // Make sure that party is in a valid range.
                // (Note: The Telephony middle layer imposes a range of 1 to 7.
                // It's sufficient for us to just make sure it's single digit.)
                if (party > 0 && party < 10) {
                    sprintf(cmd, "AT+CHLD=2%d", party);
                    at_send_command(cmd, NULL);
                    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
                } else {
                    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
                }
            }
            break;

        case RIL_REQUEST_SIGNAL_STRENGTH:
            requestSignalStrength(data, datalen, t);
            break;
        case RIL_REQUEST_VOICE_REGISTRATION_STATE:
        case RIL_REQUEST_DATA_REGISTRATION_STATE:
            requestRegistrationState(request, data, datalen, t);
            break;
        case RIL_REQUEST_OPERATOR:
            requestOperator(data, datalen, t);
            break;
        case RIL_REQUEST_RADIO_POWER:
            requestRadioPower(data, datalen, t);
            break;
        case RIL_REQUEST_DTMF: {
            char c = ((char *)data)[0];
            char *cmd;
            asprintf(&cmd, "AT+VTS=%c", (int)c);
            at_send_command(cmd, NULL);
            free(cmd);
            RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            break;
        }
        case RIL_REQUEST_SEND_SMS:
            requestSendSMS(data, datalen, t);
            break;
        case RIL_REQUEST_CDMA_SEND_SMS:
            requestCdmaSendSMS(data, datalen, t);
            break;
#if (RIL_VERSION_4_4_AND_ABOVE == 1)
        case RIL_REQUEST_IMS_REGISTRATION_STATE:
			requestImsRegistrationState(data, datalen, t);
			break;
        case RIL_REQUEST_IMS_SEND_SMS:
        	RequestImsSendSms(data, datalen, t);
        	break;
#endif

        case RIL_REQUEST_SETUP_DATA_CALL:
            requestSetupDataCall(data, datalen, t);
            break;
        // altair
		case RIL_REQUEST_DEACTIVATE_DATA_CALL:
				requestDeactivateDataCall(data, datalen, t);
				break;
        case RIL_REQUEST_SMS_ACKNOWLEDGE:
            requestSMSAcknowledge(data, datalen, t);
            break;

        case RIL_REQUEST_CDMA_SMS_ACKNOWLEDGE:
            requestCDMASMSAcknowledge(data, datalen, t);
            break;
        case RIL_REQUEST_GET_IMSI:
            p_response = NULL;
            err = at_send_command_numeric("AT+CIMI", &p_response);

            if (err < 0 || p_response->success == 0) {
                RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
            } else {
                RIL_onRequestComplete(t, RIL_E_SUCCESS,
                    p_response->p_intermediates->line, sizeof(char *));
            }
            at_response_free(p_response);
            break;

        case RIL_REQUEST_GET_IMEI:
            p_response = NULL;
            err = at_send_command_numeric("AT+CGSN", &p_response);

            if (err < 0 || p_response->success == 0) {
                RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
            } else {
                RIL_onRequestComplete(t, RIL_E_SUCCESS,
                    p_response->p_intermediates->line, sizeof(char *));
            }
            at_response_free(p_response);
            break;

        case RIL_REQUEST_SIM_IO:
            requestSIM_IO(data,datalen,t);
            break;

        case RIL_REQUEST_SEND_USSD:
            requestSendUSSD(data, datalen, t);
            break;

        case RIL_REQUEST_CANCEL_USSD:
            p_response = NULL;
            err = at_send_command_numeric("AT+CUSD=2", &p_response);

            if (err < 0 || p_response->success == 0) {
                RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
            } else {
                RIL_onRequestComplete(t, RIL_E_SUCCESS,
                    p_response->p_intermediates->line, sizeof(char *));
            }
            at_response_free(p_response);
            break;

        case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
        	requestSetNetworkSelectionAutomatic(data, datalen, t);
            break;

        case RIL_REQUEST_DATA_CALL_LIST:
            requestDataCallList(data, datalen, t);
            break;

        case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
            requestQueryNetworkSelectionMode(data, datalen, t);
            break;

        case RIL_REQUEST_OEM_HOOK_RAW:
            // echo back data
            RIL_onRequestComplete(t, RIL_E_SUCCESS, data, datalen);
            break;


        case RIL_REQUEST_OEM_HOOK_STRINGS: {
            int i;
            const char ** cur;

            RLOGD("got OEM_HOOK_STRINGS: 0x%8p %lu", data, (long)datalen);


            for (i = (datalen / sizeof (char *)), cur = (const char **)data ;
                    i > 0 ; cur++, i --) {
                RLOGD("> '%s'", *cur);
            }

            // echo back strings
            RIL_onRequestComplete(t, RIL_E_SUCCESS, data, datalen);
            break;
        }

        case RIL_REQUEST_WRITE_SMS_TO_SIM:
            requestWriteSmsToSim(data, datalen, t);
            break;

        case RIL_REQUEST_DELETE_SMS_ON_SIM: {
            char * cmd;
            p_response = NULL;
            asprintf(&cmd, "AT+CMGD=%d", ((int *)data)[0]);
            err = at_send_command(cmd, &p_response);
            free(cmd);
            if (err < 0 || p_response->success == 0) {
                RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
            } else {
                RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
            }
            at_response_free(p_response);
            break;
        }

        case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
            requestSetNetworkSelectionManual(data, datalen, t);
            break;
            
        case RIL_REQUEST_QUERY_AVAILABLE_NETWORKS:
            requestQueryAvailableNetworks(data, datalen, t);
            break;
        case RIL_REQUEST_ENTER_SIM_PIN:
        case RIL_REQUEST_ENTER_SIM_PUK:
        case RIL_REQUEST_ENTER_SIM_PIN2:
        case RIL_REQUEST_ENTER_SIM_PUK2:
        case RIL_REQUEST_CHANGE_SIM_PIN:
        case RIL_REQUEST_CHANGE_SIM_PIN2:
            requestEnterSimPin(data, datalen, t);
            break;

        case RIL_REQUEST_VOICE_RADIO_TECH:
            {
                int tech = techFromModemType(TECH(sMdmInfo));
                if (tech < 0 )
                    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
                else
                    RIL_onRequestComplete(t, RIL_E_SUCCESS, &tech, sizeof(tech));
            }
            break;
        case RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE:
            requestSetPreferredNetworkType(request, data, datalen, t);
            break;

        case RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE:
            requestGetPreferredNetworkType(request, data, datalen, t);
            break;
#if (RIL_VERSION_4_3_AND_ABOVE == 1 )
        case RIL_REQUEST_GET_CELL_INFO_LIST:
            requestGetCellInfoList(data, datalen, t);
            break;

        case RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE:
            requestSetCellInfoListRate(data, datalen, t);
            break;
#endif

#ifdef RIL_FEATURE_ENABLE_VOLTE
        case RIL_REQUEST_LAST_CALL_FAIL_CAUSE:
        	requestGetLastCallFailCause(data, datalen, t);
            break;
        case RIL_REQUEST_QUERY_CLIP:
        	requestGetClip(data, datalen, t);
            break;

        case RIL_REQUEST_GET_CLIR:
        	requestGetClir(data, datalen, t);
        	break;

        case RIL_REQUEST_SET_CLIR:
        	requestSetClir(data, datalen, t);
            break;
        case RIL_REQUEST_QUERY_CALL_WAITING:
            requestQuaryCallWaiting(data, datalen, t);
            break;
        case RIL_REQUEST_SET_CALL_WAITING:
            requestSetCallWaiting(data, datalen, t);
            break;
#endif // RIL_FEATURE_ENABLE_VOLTE
        /* CDMA Specific Requests */
        case RIL_REQUEST_BASEBAND_VERSION:
            if (TECH_BIT(sMdmInfo) == MDM_LTE) {
            	requestLteBaseBandVersion(request, data, datalen, t);
                break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_DEVICE_IDENTITY:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestCdmaDeviceIdentity(request, data, datalen, t);
                break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_CDMA_SUBSCRIPTION:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestCdmaSubscription(request, data, datalen, t);
                break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_CDMA_SET_SUBSCRIPTION_SOURCE:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestCdmaSetSubscriptionSource(request, data, datalen, t);
                break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_CDMA_GET_SUBSCRIPTION_SOURCE:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestCdmaGetSubscriptionSource(request, data, datalen, t);
                break;
            }else if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
            	RIL_onRequestComplete(t, RIL_E_SUBSCRIPTION_NOT_AVAILABLE, NULL, 0);
            	break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_CDMA_QUERY_ROAMING_PREFERENCE:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestCdmaGetRoamingPreference(request, data, datalen, t);
                break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_CDMA_SET_ROAMING_PREFERENCE:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestCdmaSetRoamingPreference(request, data, datalen, t);
                break;
            } // Fall-through if tech is not cdma

        case RIL_REQUEST_EXIT_EMERGENCY_CALLBACK_MODE:
            if (TECH_BIT(sMdmInfo) == MDM_CDMA) {
                requestExitEmergencyMode(data, datalen, t);
                break;
            } // Fall-through if tech is not cdma


        default:
            RLOGD("Request not supported. Tech: %d",TECH(sMdmInfo));
            RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
            break;
    }
}

/**
 * Synchronous call from the RIL to us to return current radio state.
 * RADIO_STATE_UNAVAILABLE should be the initial state.
 */
static RIL_RadioState
currentState()
{
    return sState;
}
/**
 * Call from RIL to us to find out whether a specific request code
 * is supported by this implementation.
 *
 * Return 1 for "supported" and 0 for "unsupported"
 */

static int
onSupports (int requestCode)
{
    //@@@ todo

    return 1;
}

static void onCancel (RIL_Token t)
{
    //@@@todo

}

static const char * getVersion(void)
{
#if (RIL_VERSION_4_3_AND_ABOVE == 0 )
    return "Altair-ril(4.2) 01_01_00_00_07";
#elif (RIL_VERSION_4_4_AND_ABOVE == 0)
    return "Altair-ril (4.3) 01_01_00_00_07";
#else
    return "Altair-ril 01_01_00_00_07";
#endif
}



#ifdef altair_remove
static void
setRadioTechnology(ModemInfo *mdm, int newtech)
{
    RLOGD("setRadioTechnology(%d)", newtech);

    int oldtech = TECH(mdm);

    if (newtech != oldtech) {
        RLOGD("Tech change (%d => %d)", oldtech, newtech);
        TECH(mdm) = newtech;
        if (techFromModemType(newtech) != techFromModemType(oldtech)) {
            int tech = techFromModemType(TECH(sMdmInfo));
            if (tech > 0 ) {
                RIL_onUnsolicitedResponse(RIL_UNSOL_VOICE_RADIO_TECH_CHANGED,
                                          &tech, sizeof(tech));
            }
        }
    }
}
#endif

static void
setRadioState(RIL_RadioState newState)
{
    RLOGD("setRadioState(%d)", newState);
    RIL_RadioState oldState;

    pthread_mutex_lock(&s_state_mutex);

    oldState = sState;

    if (s_closed > 0) {
        // If we're closed, the only reasonable state is
        // RADIO_STATE_UNAVAILABLE
        // This is here because things on the main thread
        // may attempt to change the radio state after the closed
        // event happened in another thread
        newState = RADIO_STATE_UNAVAILABLE;
    }

    if (sState != newState || s_closed > 0) {
        sState = newState;

        pthread_cond_broadcast (&s_state_cond);
    }

    pthread_mutex_unlock(&s_state_mutex);


    /* do these outside of the mutex */
    if (sState != oldState) {
        RIL_onUnsolicitedResponse (RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
                                    NULL, 0);

        /* FIXME onSimReady() and onRadioPowerOn() cannot be called
         * from the AT reader thread
         * Currently, this doesn't happen, but if that changes then these
         * will need to be dispatched on the request thread
         */
        if (sState == RADIO_STATE_ON) {
            onRadioPowerOn();
        }
    }
}

/** Returns RUIM_NOT_READY on error */
static SIM_Status
getRUIMStatus()
{
    ATResponse *p_response = NULL;
    int err;
    int ret;
    char *cpinLine;
    char *cpinResult;

    if (sState == RADIO_STATE_OFF || sState == RADIO_STATE_UNAVAILABLE) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);

    if (err != 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    switch (at_get_cme_error(p_response)) {
        case CME_SUCCESS:
            break;

        case CME_SIM_NOT_INSERTED:
            ret = SIM_ABSENT;
            goto done;

        default:
            ret = SIM_NOT_READY;
            goto done;
    }

    /* CPIN? has succeeded, now look at the result */

    cpinLine = p_response->p_intermediates->line;
    err = at_tok_start (&cpinLine);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_tok_nextstr(&cpinLine, &cpinResult);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    if (0 == strcmp (cpinResult, "SIM PIN")) {
        ret = SIM_PIN;
        goto done;
    } else if (0 == strcmp (cpinResult, "SIM PUK")) {
        ret = SIM_PUK;
        goto done;
    } else if (0 == strcmp (cpinResult, "PH-NET PIN")) {
        return SIM_NETWORK_PERSONALIZATION;
    } else if (0 != strcmp (cpinResult, "READY"))  {
        /* we're treating unsupported lock types as "sim absent" */
        ret = SIM_ABSENT;
        goto done;
    }

    at_response_free(p_response);
    p_response = NULL;
    cpinResult = NULL;

    ret = SIM_READY;

done:
    at_response_free(p_response);
    return ret;
}

/** Returns SIM_NOT_READY on error */
static SIM_Status
getSIMStatus()
{
    ATResponse *p_response = NULL;
    int err;
    int ret;
    char *cpinLine;
    char *cpinResult;

    RLOGD("getSIMStatus(). sState: %d",sState);
    if (sState == RADIO_STATE_OFF || sState == RADIO_STATE_UNAVAILABLE) {
        ret = SIM_NOT_READY;
        goto done;
    }

    /*altair_todo  - cpin & cme*/
    err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);

    if (err != 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    switch (at_get_cme_error(p_response)) {
        case CME_SUCCESS:
            break;

        case CME_SIM_NOT_INSERTED:
            ret = SIM_ABSENT;
            goto done;

        default:
            ret = SIM_NOT_READY;
            goto done;
    }

    /* CPIN? has succeeded, now look at the result */

    cpinLine = p_response->p_intermediates->line;
    err = at_tok_start (&cpinLine);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_tok_nextstr(&cpinLine, &cpinResult);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    if (0 == strcmp (cpinResult, "SIM PIN")) {
        ret = SIM_PIN;
        goto done;
    } else if (0 == strcmp (cpinResult, "SIM PUK")) {
        ret = SIM_PUK;
        goto done;
    } else if (0 == strcmp (cpinResult, "PH-NET PIN")) {
        return SIM_NETWORK_PERSONALIZATION;
    } else if (0 != strcmp (cpinResult, "READY"))  {
        /* we're treating unsupported lock types as "sim absent" */
        ret = SIM_ABSENT;
        goto done;
    }

    at_response_free(p_response);
    p_response = NULL;
    cpinResult = NULL;

    ret = SIM_READY;

done:
    at_response_free(p_response);
    return ret;
}


/**
 * Get the current card status.
 *
 * This must be freed using freeCardStatus.
 * @return: On success returns RIL_E_SUCCESS
 */
static int getCardStatus(RIL_CardStatus_v6 **pp_card_status) {
    static RIL_AppStatus app_status_array[] = {
        // SIM_ABSENT = 0
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_NOT_READY = 1
        { RIL_APPTYPE_SIM, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_READY = 2
        { RIL_APPTYPE_SIM, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_PIN = 3
        { RIL_APPTYPE_SIM, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
        // SIM_PUK = 4
        { RIL_APPTYPE_SIM, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED, RIL_PINSTATE_UNKNOWN },
        // SIM_NETWORK_PERSONALIZATION = 5
        { RIL_APPTYPE_SIM, RIL_APPSTATE_SUBSCRIPTION_PERSO, RIL_PERSOSUBSTATE_SIM_NETWORK,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
        // RUIM_ABSENT = 6
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // RUIM_NOT_READY = 7
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // RUIM_READY = 8
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // RUIM_PIN = 9
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
        // RUIM_PUK = 10
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED, RIL_PINSTATE_UNKNOWN },
        // RUIM_NETWORK_PERSONALIZATION = 11
        { RIL_APPTYPE_RUIM, RIL_APPSTATE_SUBSCRIPTION_PERSO, RIL_PERSOSUBSTATE_SIM_NETWORK,
           NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN }
    };
    RIL_CardState card_state;
    int num_apps;

    int sim_status = getSIMStatus();
    if (sim_status == SIM_ABSENT) {
        card_state = RIL_CARDSTATE_ABSENT;
        num_apps = 0;
    } else {
        card_state = RIL_CARDSTATE_PRESENT;
        num_apps = 2;
    }

    // Allocate and initialize base card status.
    RIL_CardStatus_v6 *p_card_status = malloc(sizeof(RIL_CardStatus_v6));
    p_card_status->card_state = card_state;
    p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
    p_card_status->gsm_umts_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->ims_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->num_applications = num_apps;

    // Initialize application status
    int i;
    for (i = 0; i < RIL_CARD_MAX_APPS; i++) {
        p_card_status->applications[i] = app_status_array[SIM_ABSENT];
    }

    // Pickup the appropriate application status
    // that reflects sim_status for gsm.
    if (num_apps != 0) {
        // Only support one app, gsm
        p_card_status->num_applications = num_apps;
        p_card_status->gsm_umts_subscription_app_index = 0;
        p_card_status->cdma_subscription_app_index = 1;

        // Get the correct app status
        p_card_status->applications[0] = app_status_array[sim_status];
        p_card_status->applications[1] = app_status_array[sim_status + RUIM_ABSENT];
    }

    *pp_card_status = p_card_status;
    return RIL_E_SUCCESS;
}

/**
 * Free the card status returned by getCardStatus
 */
static void freeCardStatus(RIL_CardStatus_v6 *p_card_status) {
    free(p_card_status);
}

/**
 * SIM ready means any commands that access the SIM will work, including:
 *  AT+CPIN, AT+CSMS, AT+CNMI, AT+CRSM
 *  (all SMS-related commands)
 */

static void pollSIMState (void *param)
{
    ATResponse *p_response=NULL;
    int ret;

    if (sState != RADIO_STATE_SIM_NOT_READY) {
        // no longer valid to poll
        return;
    }

    switch(getSIMStatus()) {
        case SIM_ABSENT:
        case SIM_PIN:
        case SIM_PUK:
        case SIM_NETWORK_PERSONALIZATION:
        default:
            RLOGI("SIM ABSENT or LOCKED");
            RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
        return;

        case SIM_NOT_READY:
            RIL_requestTimedCallback (pollSIMState, NULL, &TIMEVAL_SIMPOLL);
        return;

        case SIM_READY:
            RLOGI("SIM_READY");
            onSIMReady();
            RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
        return;
    }
}

/** returns 1 if on, 0 if off, and -1 on error */
static int isRadioOn()
{
    ATResponse *p_response = NULL;
    int err;
    char *line;
    char ret;

    err = at_send_command_singleline("AT+CFUN?", "+CFUN:", &p_response);

    if (err < 0 || p_response->success == 0) {
        // assume radio is off
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextbool(&line, &ret);
    if (err < 0) goto error;

    at_response_free(p_response);

    return (int)ret;

error:

    at_response_free(p_response);
    return -1;
}



#ifdef altair_remove
/**
 * Parse the response generated by a +CTEC AT command
 * The values read from the response are stored in current and preferred.
 * Both current and preferred may be null. The corresponding value is ignored in that case.
 *
 * @return: -1 if some error occurs (or if the modem doesn't understand the +CTEC command)
 *          1 if the response includes the current technology only
 *          0 if the response includes both current technology and preferred mode
 */
int parse_technology_response( const char *response, int *current, int32_t *preferred )
{
    int err;
    char *line, *p;
    int ct;
    int32_t pt = 0;
    char *str_pt;

    line = p = strdup(response);
    RLOGD("Response: %s", line);
    err = at_tok_start(&p);
    if (err || !at_tok_hasmore(&p)) {
        RLOGD("err: %d. p: %s", err, p);
        free(line);
        return -1;
    }

    err = at_tok_nextint(&p, &ct);
    if (err) {
        free(line);
        return -1;
    }
    if (current) *current = ct;

    RLOGD("line remaining after int: %s", p);

    err = at_tok_nexthexint(&p, &pt);
    if (err) {
        free(line);
        return 1;
    }
    if (preferred) {
        *preferred = pt;
    }
    free(line);

    return 0;
}

int query_supported_techs( ModemInfo *mdm, int *supported )
{
    ATResponse *p_response=NULL;
    int err, val, techs = 0;
    char *tok;
    char *line;

    RLOGD("query_supported_techs");
    err = at_send_command_singleline("AT+CTEC=?", "+CTEC:", &p_response);
    if (err || !p_response->success)
        goto error;
    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err || !at_tok_hasmore(&line))
        goto error;
    while (!at_tok_nextint(&line, &val)) {
        techs |= ( 1 << val );
    }
    if (supported) *supported = techs;
    return 0;
error:
    at_response_free(p_response);
    return -1;
}

/**
 * query_ctec. Send the +CTEC AT command to the modem to query the current
 * and preferred modes. It leaves values in the addresses pointed to by
 * current and preferred. If any of those pointers are NULL, the corresponding value
 * is ignored, but the return value will still reflect if retreiving and parsing of the
 * values suceeded.
 *
 * @mdm Currently unused
 * @current A pointer to store the current mode returned by the modem. May be null.
 * @preferred A pointer to store the preferred mode returned by the modem. May be null.
 * @return -1 on error (or failure to parse)
 *         1 if only the current mode was returned by modem (or failed to parse preferred)
 *         0 if both current and preferred were returned correctly
 */
int query_ctec(ModemInfo *mdm, int *current, int32_t *preferred)
{
    ATResponse *response = NULL;
    int err;
    int res;

    RLOGD("query_ctec. current: %d, preferred: %d", (int)current, (int) preferred);
    err = at_send_command_singleline("AT+CTEC?", "+CTEC:", &response);
    if (!err && response->success) {
        res = parse_technology_response(response->p_intermediates->line, current, preferred);
        at_response_free(response);
        return res;
    }
    RLOGE("Error executing command: %d. response: %x. status: %d", err, (int)response, response? response->success : -1);
    at_response_free(response);
    return -1;
}

int is_multimode_modem(ModemInfo *mdm)
{
    ATResponse *response=NULL;
    int err;
    char *line;
    int tech;
    int32_t preferred;

    if (query_ctec(mdm, &tech, &preferred) == 0) {
        mdm->currentTech = tech;
        mdm->preferredNetworkMode = preferred;
        if (query_supported_techs(mdm, &mdm->supportedTechs)) {
            return 0;
        }
        return 1;
    }
    return 0;
}
#endif

/**
 * Find out if our modem is GSM, CDMA or both (Multimode)
 */
static void probeForModemMode(ModemInfo *info)
{
	// Altair - configure ModemInfo to LTE only
	info->isMultimode 			= 0;  			// not Multimode
	info->supportedTechs 		= MDM_LTE;
	info->currentTech 			= MDM_LTE ;   // we do not use the notation MDM_LTE=1<<4;
	RLOGI("Found LTE Modem");

}

/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=0
 */
static void initializeCallback(void *param)
{

	ATResponse *p_response = NULL;
    int err;

    setRadioState (RADIO_STATE_OFF);

    at_handshake();

    /* Altair_todo do we need periodic AT command ? */
    /* Altair_todo disable periodic AT check if AT log is open */

    probeForModemMode(sMdmInfo);

    /* note: we don't check errors here. Everything important will
       be handled in onATTimeout and onATReaderClosed */
    

    if (isDelayAtChannel()!=0){
    	RLOGD("start: wait 60 sec on init");
    	sleep(60) ; // for debug purposes wait for 10 sec
    	at_send_command("at", NULL);
    	RLOGD("finish: wait on init");
    }


    /*  echo off */
    at_send_command("ATE0", NULL);
    /*  verbose result codes */
    at_send_command("ATQ0", NULL);
    /*  Extended errors */
    at_send_command("AT+CMEE=1", NULL);
    /*  Network registration events */
	at_send_command("AT+CEREG=2", NULL);
	/* ECM connection state events */
	at_send_command("AT%STATCM=1", NULL);
    /*  SMS PDU mode */
    at_send_command("AT+CMGF=0", NULL);
    /*  NITZ events */
    at_send_command("AT%NOTIFYEV=\"LTIME\",1",NULL);
#ifdef RIL_FEATURE_ENABLE_VOLTE
    at_send_command("AT+CLIP=1", NULL);
    /*  +CSSU unsolicited supp service notifications */
    at_send_command("AT+CSSN=0,1", NULL);
#endif

#if (RIL_VERSION_4_4_AND_ABOVE == 1)
    /* check for ims registration */
    at_send_command("AT+CIREG=1", NULL);
#endif

#if altair_remove  // the following are not required for the supported feature set
    /*  GPRS registration events */
    at_send_command("AT+CGREG=1", NULL);

    /*  Call Waiting notifications */
    at_send_command("AT+CCWA=1", NULL);

    /*  Alternating voice/data off */
    at_send_command("AT+CMOD=0", NULL);

    /*  Not muted */
    at_send_command("AT+CMUT=0", NULL);

    /*  no connected line identification */
    at_send_command("AT+COLP=0", NULL);

    /*  HEX character set */
    at_send_command("AT+CSCS=\"HEX\"", NULL);

    /*  USSD unsolicited */
    at_send_command("AT+CUSD=1", NULL);

    /*  Enable +CGEV GPRS event notifications, but don't buffer */
    at_send_command("AT+CGEREP=1,0", NULL);

#endif 

    /* altair todo - do we need to add usin modify notification to be used in onuncolicitate ?*/

    /* assume radio is off on error */
    if (isRadioOn() > 0) {
    	/*remove*/ RLOGI("initializeCallback - radio is on");
    	// on CDMA phone test case , if the sim is not available , the framework create a CDMAphone  , than since phone state is *sim* it starts to destruct the cdmaPhone and construct the gsmPhone , then access ( the partly distracted) cdmaPhone and crashes
    	//setRadioState (RADIO_STATE_SIM_NOT_READY);
    	setRadioState (RADIO_STATE_ON);
    }

    
}

static void waitForClose()
{
    pthread_mutex_lock(&s_state_mutex);

    while (s_closed == 0) {
        pthread_cond_wait(&s_state_cond, &s_state_mutex);
    }

    pthread_mutex_unlock(&s_state_mutex);
}

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */
static void onUnsolicited (const char *s, const char *sms_pdu)
{
    char *line = NULL, *p;
    int err;
    int isProcessedMessage = 1; // by default assume the message was processed

    /* Ignore unsolicited responses until we're initialized.
     * This is OK because the RIL library will poll for initial state
     */
    //RLOGD("onUnsolicited received[%s]",s);
    if (sState == RADIO_STATE_UNAVAILABLE) {
        return;
    }

    if (strStartsWith(s, "%CTZV:")) {
        /* TI specific -- NITZ time */
        char *response;

        line = p = strdup(s);
        at_tok_start(&p);

        err = at_tok_nextstr(&p, &response);

        free(line);
        if (err != 0) {
            RLOGE("invalid NITZ line %s\n", s);
        } else {
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_NITZ_TIME_RECEIVED,
                response, strlen(response));
        }
    } else if (strStartsWith(s,"+CRING:")
                || strStartsWith(s,"RING")
                || strStartsWith(s,"NO CARRIER")
                || strStartsWith(s,"+CCWA")
                || strStartsWith(s,"+CDUU") /* ignore all parameters , the framework will inquiry the changes */
    ) {
        RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
            NULL, 0);
#ifdef WORKAROUND_FAKE_CGEV
        RIL_requestTimedCallback (onDataCallListChanged, NULL, NULL); //TODO use new function
#endif /* WORKAROUND_FAKE_CGEV */
    } else if (strStartsWith(s,"+CREG:")
                || strStartsWith(s,"+CGREG:") || strStartsWith(s,"+CEREG:")
    ) {
        RIL_onUnsolicitedResponse (
            RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
            NULL, 0);
#ifdef WORKAROUND_FAKE_CGEV
        RIL_requestTimedCallback (onDataCallListChanged, NULL, NULL);
#endif /* WORKAROUND_FAKE_CGEV */

#if (RIL_VERSION_4_4_AND_ABOVE == 1)
    }else if (strStartsWith(s,"+CIREGU:"))
    {
    	RIL_onUnsolicitedResponse (
    			    RIL_UNSOL_RESPONSE_IMS_NETWORK_STATE_CHANGED,
    	            NULL, 0);
#endif
    }else if (strStartsWith(s,"%STATCM:"))
    {
        // STATCM is Embedded connection manager events
        char *line = NULL, *p;
        int eventId;
        line = p = strdup(s);
        if (!line) {
            RLOGE("STATCM: Unable to allocate memory");
            return;
        }
        if (at_tok_start(&p) < 0) {
            free(line);
            return ;
        }

        if (at_tok_nextint(&p, &eventId) < 0) {
            RLOGE("invalid STATCM response: %s", line);
            free(line);
            return ;
        }

        free(line);
        // STATCM:0 - lte disconnected , STATCM: 1 - lte connected
        // STATCM:3 - PDN connected - STATCM:4 - PDN disconnected.
        // we only care about pdn disconnection (0/1 are covered by +CEREG and 3 is always 
        // user triggered - thus nothing to notify)
        // notify CALL list state change
        if ((eventId == 4) )
        {
            RIL_requestTimedCallback (onDataCallListChanged, NULL, NULL);
        }
    }else if (strStartsWith(s, "+CMT:")) {
// altair - check if this is a GSM of CDMA SMS and report up
        int isGsm = isGSMSmsUnsolicited(s,sms_pdu);
        if (isGsm < 0)
            return;
         
        if (isGsm)
        {
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_RESPONSE_NEW_SMS,
                sms_pdu, strlen(sms_pdu));
        }
        else
        {
            notifynewCdmaSMS(sms_pdu);
        }
    } else if (strStartsWith(s, "+CDS:")) {
// altair - check if this GSM of CDMA Status Report indication
        int isGsm = isGSMSmsUnsolicited(s,sms_pdu);
        if (isGsm < 0)
            return;
         
        if (isGsm)
        {
            RIL_onUnsolicitedResponse (
                RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT,
                sms_pdu, strlen(sms_pdu));
        }
        else
        {
            notifynewCdmaSMS(sms_pdu);
        }
    } else if (strStartsWith(s, "+CGEV:")) {
        /* Really, we can ignore NW CLASS and ME CLASS events here,
         * but right now we don't since extranous
         * RIL_UNSOL_DATA_CALL_LIST_CHANGED calls are tolerated
         */
        /* can't issue AT commands here -- call on main thread */
        RIL_requestTimedCallback (onDataCallListChanged, NULL, NULL);
#ifdef WORKAROUND_FAKE_CGEV
    } else if (strStartsWith(s, "+CME ERROR: 150")) {
        RIL_requestTimedCallback (onDataCallListChanged, NULL, NULL);
#endif /* WORKAROUND_FAKE_CGEV */
#ifdef altair_remove
    } else if (strStartsWith(s, "+CTEC: ")) {
        int tech, mask;
        switch (parse_technology_response(s, &tech, NULL))
        {
            case -1: // no argument could be parsed.
                RLOGE("invalid CTEC line %s\n", s);
                break;
            case 1: // current mode correctly parsed
            case 0: // preferred mode correctly parsed
                mask = 1 << tech;
                if (mask != MDM_GSM && mask != MDM_CDMA &&
                     mask != MDM_WCDMA && mask != MDM_LTE) {
                    RLOGE("Unknown technology %d\n", tech);
                } else {
                    setRadioTechnology(sMdmInfo, tech);
                }
                break;
        }
#endif
    } else if (strStartsWith(s, "+CCSS: ")) {
        int source = 0;
        line = p = strdup(s);
        if (!line) {
            RLOGE("+CCSS: Unable to allocate memory");
            return;
        }
        if (at_tok_start(&p) < 0) {
            free(line);
            return;
        }
        if (at_tok_nextint(&p, &source) < 0) {
            RLOGE("invalid +CCSS response: %s", line);
            free(line);
            return;
        }
        SSOURCE(sMdmInfo) = source;
        RIL_onUnsolicitedResponse(RIL_UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED,
                                  &source, sizeof(source));
    } else if (strStartsWith(s, "+WSOS: ")) {
        char state = 0;
        int unsol;
        line = p = strdup(s);
        if (!line) {
            RLOGE("+WSOS: Unable to allocate memory");
            return;
        }
        if (at_tok_start(&p) < 0) {
            free(line);
            return;
        }
        if (at_tok_nextbool(&p, &state) < 0) {
            RLOGE("invalid +WSOS response: %s", line);
            free(line);
            return;
        }
        free(line);

        unsol = state ?
                RIL_UNSOL_ENTER_EMERGENCY_CALLBACK_MODE : RIL_UNSOL_EXIT_EMERGENCY_CALLBACK_MODE;

        RIL_onUnsolicitedResponse(unsol, NULL, 0);

    } else if (strStartsWith(s, "+WPRL: ")) {
        int version = -1;
        line = p = strdup(s);
        if (!line) {
            RLOGE("+WPRL: Unable to allocate memory");
            return;
        }
        if (at_tok_start(&p) < 0) {
            RLOGE("invalid +WPRL response: %s", s);
            free(line);
            return;
        }
        if (at_tok_nextint(&p, &version) < 0) {
            RLOGE("invalid +WPRL response: %s", s);
            free(line);
            return;
        }
        free(line);
        RIL_onUnsolicitedResponse(RIL_UNSOL_CDMA_PRL_CHANGED, &version, sizeof(version));
    } else if (strStartsWith(s, "+CFUN: 0")) {
        setRadioState(RADIO_STATE_OFF);
    } else if (strStartsWith(s, "%NOTIFYEV:LTIME")) {
    	char *skip = NULL;
    	line = strdup(s);
    	at_tok_start(&line);
		err = at_tok_nextstr(&line, &skip);  //skip LTIME

		if (err != 0) {
			RLOGE("invalid NITZ line %s\n", s);
		} else {
			RLOGI("update NITZ time: %s\n", line);
			RIL_onUnsolicitedResponse (RIL_UNSOL_NITZ_TIME_RECEIVED,line, strlen(line));
		}
    }else {
    	isProcessedMessage = -1; // the message was ignored
    }

    if (isProcessedMessage>0)    {
    	RLOGE("onUnsolicited: %s", s);
    }

    if (onUnsolicitedAtSocketUserdata) {
        altairAtSocketOnUnsolicited(s,onUnsolicitedAtSocketUserdata);
    }
}

/* Called on command or reader thread */
static void onATReaderClosed()
{
    RLOGI("AT channel closed\n");
    at_close();
    s_closed = 1;

    setRadioState (RADIO_STATE_UNAVAILABLE);
}

/* Called on command thread */
static void onATTimeout()
{
    RLOGI("AT channel timeout; closing\n");
    at_close();

    s_closed = 1;

    /* FIXME cause a radio reset here */

    setRadioState (RADIO_STATE_UNAVAILABLE);
}

static void usage(char *s)
{
#ifdef RIL_SHLIB
    fprintf(stderr, "reference-ril requires: -p <tcp port> or -d /dev/tty_device\n");
#else
    fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device] [-r <remote modem ip> \n", s);
    exit(-1);
#endif
}

static void *
mainLoop(void *param)
{
    int fd;
    int ret;

    AT_DUMP("== ", "entering mainLoop()", -1 );
    at_set_on_reader_closed(onATReaderClosed);
    at_set_on_timeout(onATTimeout);

    for (;;) {
        fd = -1;
        while  (fd < 0) {
        	if (s_remote_ip_addr !=NULL){
        		if (s_port <= 0) // if -p option is missing assume default port 6666
        		{
        			s_port = 6666;
        		}
        		fd = socket_network_client(s_remote_ip_addr,s_port, SOCK_STREAM);
        		RLOGE ("connectiong to remote modem at %s : %d\n", s_remote_ip_addr,s_port);
        	}else if (s_port > 0) {
                fd = socket_loopback_client(s_port, SOCK_STREAM);
            } else if (s_device_socket) {
                if (!strcmp(s_device_path, "/dev/socket/qemud")) {
                    /* Before trying to connect to /dev/socket/qemud (which is
                     * now another "legacy" way of communicating with the
                     * emulator), we will try to connecto to gsm service via
                     * qemu pipe. */
                    fd = qemu_pipe_open("qemud:gsm");
                    if (fd < 0) {
                        /* Qemu-specific control socket */
                        fd = socket_local_client( "qemud",
                                                  ANDROID_SOCKET_NAMESPACE_RESERVED,
                                                  SOCK_STREAM );
                        if (fd >= 0 ) {
                            char  answer[2];

                            if ( write(fd, "gsm", 3) != 3 ||
                                 read(fd, answer, 2) != 2 ||
                                 memcmp(answer, "OK", 2) != 0)
                            {
                                close(fd);
                                fd = -1;
                            }
                       }
                    }
                }
                else
                    fd = socket_local_client( s_device_path,
                                            ANDROID_SOCKET_NAMESPACE_FILESYSTEM,
                                            SOCK_STREAM );
            } else if (s_device_path != NULL) {
                fd = open (s_device_path, O_RDWR);
                if ( fd >= 0 && !memcmp( s_device_path, "/dev/tty", 8 ) ) {
                    /* disable echo on serial ports */
                    struct termios  ios;
                    tcgetattr( fd, &ios );
                    ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
                    tcsetattr( fd, TCSANOW, &ios );
                }
            }

            if (fd < 0) {
                perror ("opening AT interface. retrying...");
                sleep(10);
                /* never returns */
            }
        }

        s_closed = 0;
        ret = at_open(fd, onUnsolicited);

        if (ret < 0) {
            RLOGE ("AT error %d on at_open\n", ret);
            return 0;
        }

        RIL_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

        // Give initializeCallback a chance to dispatched, since
        // we don't presently have a cancellation mechanism
        sleep(1);

        altairAtSocketCreate();

        waitForClose();
        RLOGI("Re-opening after close");
    }
}

#ifdef RIL_SHLIB

pthread_t s_tid_mainloop;

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;
    pthread_attr_t attr;

    s_rilenv = env;

    while ( -1 != (opt = getopt(argc, argv, "p:d:s:r:"))) {
        switch (opt) {
            case 'p':
                s_port = atoi(optarg);
                if (s_port == 0) {
                    usage(argv[0]);
                    return NULL;
                }
                RLOGI("using port port %d\n", s_port);
            break;

            case 'd':
                s_device_path = optarg;
                RLOGI("Opening tty device %s\n", s_device_path);
            break;

            case 's':
                s_device_path   = optarg;
                s_device_socket = 1;
                RLOGI("Opening socket %s\n", s_device_path);
            break;

            case 'r':
            	// added by Altair to remote AT control for remote modem, use -i<ip addr> -p<port>
            	s_remote_ip_addr = strdup(optarg);
            	RLOGI("Opening remote modem [%s]\n",s_remote_ip_addr);
            break;

            default:
                usage(argv[0]);
                return NULL;
        }
    }

    if (s_port < 0 && s_device_path == NULL) {
        usage(argv[0]);
        return NULL;
    }

    sMdmInfo = calloc(1, sizeof(ModemInfo));
    if (!sMdmInfo) {
        RLOGE("Unable to alloc memory for ModemInfo");
        return NULL;
    }
    pthread_attr_init (&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&s_tid_mainloop, &attr, mainLoop, NULL);

    return &s_callbacks;
}
#else /* RIL_SHLIB */
int main (int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;

    while ( -1 != (opt = getopt(argc, argv, "p:d:"))) {
        switch (opt) {
            case 'p':
                s_port = atoi(optarg);
                if (s_port == 0) {
                    usage(argv[0]);
                }
                RLOGI("Opening loopback port %d\n", s_port);
            break;

            case 'd':
                s_device_path = optarg;
                RLOGI("Opening tty device %s\n", s_device_path);
            break;

            case 's':
                s_device_path   = optarg;
                s_device_socket = 1;
                RLOGI("Opening socket %s\n", s_device_path);
            break;

            default:
                usage(argv[0]);
        }
    }

    if (s_port < 0 && s_device_path == NULL) {
        usage(argv[0]);
    }

    RIL_register(&s_callbacks);

    mainLoop(NULL);

    return 0;
}

#endif /* RIL_SHLIB */

/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL
 *
 * Manually select a specified network.
 *
 * The radio baseband/RIL implementation will try to camp on the manually
 * selected network regardless of coverage, i.e. there is no fallback to
 * automatic network selection.
 */
void requestSetNetworkSelectionManual(void *data, size_t datalen, RIL_Token t)
{
    int err = 0;
    char *cmd = NULL;
    const char *mccMnc = (const char *) data;
    int mode = -1;
    char cPlmn[20];
    cPlmn[0]= '\0';
    ATResponse *p_response=NULL;


    err = getConnectModeAndPlmnName(&mode, cPlmn);

    if (mode != 1 || strcmp(cPlmn,mccMnc))
    {
            
        err = disconnectLTE();
        if (err != 0 ) 
        {
            RLOGE("requestSetNetworkSelectionManual: disconnectLTE failed");    
            goto error;
        }
            
        /* manual selection connect command using numeric plmn name. */
        asprintf(&cmd, "AT+COPS=1,2,\"%s\"", mccMnc);    
        
        err = at_send_command(cmd, &p_response);
        
        if (cmd != NULL) 
            free(cmd);

        if (err < 0 || p_response->success == 0)
        {
            RLOGE("requestSetNetworkSelectionManual: Failed to set manual connection");  
            at_response_free(p_response);  
            goto error;
        }
        
        at_response_free(p_response);
    }

	// activate the connection back.
    err = connectLTE();
    if (err != 0 ) 
    {		
        goto error;
    }		

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);     
    return;

error:
	RLOGE(" ** requestSetNetworkSelectionManual Failed **");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);   
}

/**
 * RIL_REQUEST_QUERY_AVAILABLE_NETWORKS
 *
 * Scans for available networks.
 */
void requestQueryAvailableNetworks(void *data, size_t datalen, RIL_Token t)
{
    static char *statStr[] = {
        "unknown",
        "available",
        "current",
        "forbidden"
    };
    
    int err, count, curCount, stat;
    char *line, *tok;
    char **response, **cur;
    ATResponse *p_response = NULL;

    char *cmd = NULL;			
		
	err = disconnectLTE();
	if (err != 0 ) 
	{
		RLOGE("requestQueryAvailableNetworks: disconnectLTE failed");    
		goto error;
	}
			
	// get available PLMNs
    err = at_send_command_singleline("AT+COPS=?", "+COPS:",&p_response);

    if (err != 0 || p_response->success == 0) 
    {
		RLOGE("requestQueryAvailableNetworks: Failed to search");    
		goto error;
	}

    line = p_response->p_intermediates->line;

    //+COPS: (0,"S.F.R. (UMTS)","S.F.R. (UMTS)","20811"),,(0-4),(0-2)
    for (count = 0; *line; line++)
        if (*line == ')')
            count++;        

    // ignore  2 last elements.
	count -= 2;

    response = alloca(count * 4 * sizeof(char *));
    if (!response) goto error;

    line = p_response->p_intermediates->line;
    cur = response;
    curCount = 0;

    while ( (curCount < count) && (line = strchr(line, '(')) )
    {
        line++;

        err = at_tok_nextint(&line, &stat);
        if (err < 0) continue;

        cur[3] = statStr[stat];

        err = at_tok_nextstr(&line, &(cur[0]));
        if (err < 0) continue;

        err = at_tok_nextstr(&line, &(cur[1]));
        if (err < 0) continue;

        err = at_tok_nextstr(&line, &(cur[2]));
        if (err < 0) continue;
            cur += 4;
        
        curCount++;
    }
    //err = at_send_command("AT+COPS=0", NULL);
    err = connectLTE();
    if (err != 0 ) 
    {		
        goto error;
    }		

    RIL_onRequestComplete(t, RIL_E_SUCCESS, response, curCount * 4 * sizeof(char *));
    at_response_free(p_response);
   
    return;
error:
    RLOGE("requestQueryAvailableNetworks Failed");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
}

static void requestCDMASMSAcknowledge(void *data, size_t datalen, RIL_Token t)
{
    int err;
    RIL_CDMA_SMS_Ack* rcsa;

    // verify data content to test marshalling/unmarshalling:
    rcsa = (RIL_CDMA_SMS_Ack*)data;

    if (rcsa->uErrorClass == RIL_CDMA_SMS_NO_ERROR) {
        //altair - acknowledge with no error
        err = at_send_command("AT+CNMA=1", NULL);
    } else {
        //altair - acknowledge with error
        err = at_send_command("AT+CNMA=2", NULL);
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}

//altiar - check if this SMS is a GSM or CDMA SMS 
static int isGSMSmsUnsolicited(const char *s,const char *sms_pdu)
{
    char *line = NULL, *p;
    char* skip;
    int msgLength = 0;
    line = p = strdup(s);
    if (!line) {
        RLOGE("+CMT +CDS: Unable to allocate memory");
        return -1;
    }
    if (at_tok_start(&p) < 0) {
        free(line);
        return -1;
    }

    if (at_tok_nextstr(&p, &skip) < 0) {
        RLOGE("invalid +CMT +CDS response: %s", line);
        free(line);
        return -1;
    }

    if (at_tok_nextint(&p, &msgLength) < 0) {
        RLOGE("invalid +CMT +CDS response: %s", line);
        free(line);
        return -1;
    }
    free(line);
    // if the length given is not the same as the PDU length it means this is a GSM sms 
    // since the length doesn't count the SMSC length
    return ((msgLength*2) != (int)strlen(sms_pdu));
}

/*
 * The following function was added by Altair
 */


/*
 * getPdnTable
 * 		read the APN DB from the modem.
 * input:
 * output: pdnTblp the PDN data read from modem.
 * return: -1 on error
 */
static int getPdnTable(PdnTable *pdnTblp)
{
	int err;
	char *line;
	ATResponse *p_response = NULL;
	ATLine *p_cur;
	int n=0;
	char *out;
	int ignoreInt;

	// GET PDN table form modem , usedint he at%PDNACT? command.
	err = at_send_command_multiline("AT%PDNACT?", "%PDNACT",&p_response);
	if (err != 0 || p_response->success == 0)
		goto error;
	for (p_cur = p_response->p_intermediates; p_cur != NULL ;
	     p_cur = p_cur->p_next) {
		char *line = p_cur->line;
		// in case the modem has more PDN than RIL MAX , ignore them
		if (n>= MAX_NUM_OF_PDN)
		{
			RLOGE("RIL MAX_NUM_OF_PDN %d excedded, some PDN are ignored", MAX_NUM_OF_PDN);
			break;
		}
		//parse %PDNACT:<sessionID>,<stat>,<APN>,<cid>]
		err = at_tok_start(&line);
		if (err < 0)
			goto error;

		err = at_tok_nextint(&line, &pdnTblp->pdn[n].sessionId);
		if (err < 0)
			goto error;

		err = at_tok_nextint(&line, &pdnTblp->pdn[n].active);
		if (err < 0)
			goto error;

		err = at_tok_nextstr(&line, &out);
		if (err < 0)
			goto error;
		strcpy(pdnTblp->pdn[n].APN, out);

		err = at_tok_nextint(&line, &pdnTblp->pdn[n].cid);
		if (err < 0)
			goto error;

        RLOGD("sessionId=%d, APN=%s, cid=%d, active=%d",pdnTblp->pdn[n].sessionId,pdnTblp->pdn[n].APN,pdnTblp->pdn[n].cid,pdnTblp->pdn[n].active);
		n++;
	}
	pdnTblp->numOfEntries = n;
	RLOGD("numOfEntries=%d",pdnTblp->numOfEntries);
	return 0;

error:
	RLOGE("error itialising PDN table");
	return -1;
}


/*
 * getPdnByApnName
 * 		search the apn table , looking for the apn.
 * input: apn - the APN name to be searched
 * output: pdn - the pdn table entry matching the APN
 * return:
 * 		if found the sessionid is returned
 * 		if not found the apn is set useing %apnn to be the sessionid=1 , and sessionid=1 is returned
 * 		-1 on error
 */
int getPdnByApnName(char *apn,PdnTableEntry* pdn)
{
	PdnTable pdnTbl;
	int i;

	if (getPdnTable(&pdnTbl)<0)
		goto error;

	for (i=0;i<pdnTbl.numOfEntries;i++){
		if ( strcasecmp(apn,pdnTbl.pdn[i].APN)==0){

			memcpy(pdn,&pdnTbl.pdn[i],sizeof(PdnTableEntry));
            return(pdnTbl.pdn[i].sessionId);
		}
	}

error:
	RLOGE("error in getPdnByApnName");
	return -1;
}

/*
 * activateDataSessionPdn
 *
 * input: sessionId - the session ID to activate
 * output:
 * return: -1 on error
 */
static int activateDataSessionPdn(int sessionId)
{

	int err;
    ATResponse *p_response = NULL;
    char *cmd;

    if (isDelayAtChannel()!=0){
    	RLOGD("start: pause before pdnact=1");
    	sleep(5) ; // for debug purposes wait for 10 sec
    	at_send_command("at", NULL);
    	RLOGD("finish: pause before pdnact=1");
    }


    // altair - send Internet (Data) PDN connect command
    asprintf(&cmd, "AT%%PDNACT=1,%d", sessionId);
    err = at_send_command(cmd, &p_response);
    free(cmd);

    if (isDelayAtChannel()!=0){
    	RLOGD("start: pause after pdnact=1");
    	sleep(5) ; // for debug purposes wait for 10 sec
    	at_send_command("at", NULL);
    	RLOGD("finish: pause after pdnact=1",sessionId);
    }

    if (err != 0 || p_response->success == 0)
    {
        RLOGD("activateDataSessionPdn[%d]: error in activating data connection",sessionId);
        goto exit;
    }
    if (ifc_config(IFNAME(sessionId),IF_STATE_UP)<0)
    	goto exit;

     at_response_free(p_response);
     return 0;

exit:
    at_response_free(p_response);
	return -1;
}

/*
 * disconnectLTE
 *
 * input:
 * output:
 * return: -1 on error
 */
static int disconnectLTE()
{
    int err = 0;
    ATResponse *p_response = NULL;
    int connectLTE;

    err = at_send_command("AT%CMATT=0", &p_response);
    if (err != 0 || p_response->success == 0)
        RLOGE("disconnectLTE: failed ");
    at_response_free(p_response);
    return err;
}

/*
 * disconnectLTE
 *
 * input:
 * output:
 * return: -1 on error
 */
static int connectLTE()
{
    int err = 0;
    ATResponse *p_response = NULL;
    int connectLTE;

    err = at_send_command("AT%CMATT=1",&p_response);

    if (err != 0 || p_response->success == 0)
        RLOGE("connectLTE: faied");
    at_response_free(p_response);
    return err;
}

/*
 * Get current connection mode and plmn name
 */

/*
 * getConnectModeAndPlmnName
 * 		Get current connection mode and plmn name
 * input:
 * output:  mode     - 0==automatic, 1==manual
 * 			plmnName - current plmn name
 * return: -1 on error
 */
static int getConnectModeAndPlmnName(int* mode, char* plmnName)
{
    int err;
    int skip;
    char *line;
    ATResponse *p_response = NULL;
    char	*tempPlmnName;

    //set the COPS result format to numeric.
	err = at_send_command("AT+COPS=3,2", NULL);
	if (err != 0) goto error;

    // get the mode and plmn id
	err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);
    if (err != 0 || p_response->success == 0)
		goto error;

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

    // extract mode (0==automatic, 1==manual)
	err = at_tok_nextint(&line, mode);
	if (err < 0) goto error;

	// If we're unregistered, we may just get
	// a "+COPS: 0" response
	if (at_tok_hasmore(&line))
	{
		// skip format field
        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;

 	    // a "+COPS: 0, n" response is also possible
        if (at_tok_hasmore(&line))
        {
	       	err = at_tok_nextstr(&line, &tempPlmnName);
		    if (err < 0) goto error;
		    strcpy(plmnName, tempPlmnName);
		}
		else
			// missing parameter
			goto error;
	}
    else
    {
        strcpy(plmnName, "");
    }

    RLOGD("getConnectModeAndPlmn: mode[%d] ,plmnName[%s]" , *mode, plmnName);
	at_response_free(p_response);
    return 0;

error:
    RLOGE("getConnectModeAndPlmn: failed");
    at_response_free(p_response);
    return -1;

}

/*
 * requestSetNetworkSelectionAutomatic
 * 		handle RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC request
 * 		pecify that the network should be selected automatically.
 * 		note that for the +cops command to scan correctly , we need to disable the %CMATT first.
 */
void requestSetNetworkSelectionAutomatic(void *data, size_t datalen, RIL_Token t)
{
	int err;
    int mode = -1;
    char cPlmn[20];

    err = getConnectModeAndPlmnName(&mode, cPlmn);

    if (mode != 0)
    {

        err = disconnectLTE();
        if (err != 0 )
        {
            RLOGE("requestSetNetworkSelectionAutomatic: disconnectLTE failed");
            goto error;
        }

        // Set connection Automatic
        err = at_send_command("AT+COPS=0", NULL);
        if (err != 0 )
        {
            RLOGE("requestSetNetworkSelectionAutomatic: Failed to Set connection Automatic");
            goto error;
        }

        err = connectLTE();
        if (err != 0 )
        {
        	RLOGE("requestSetNetworkSelectionAutomatic: connectLTE failed");
            goto error;
        }
    }

    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    return;

error:
    RLOGE("requestSetNetworkSelectionAutomatic: failed");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;

}

/*
 * requestDeactivateDataCall
 * 		handle RIL_REQUEST_DEACTIVATE_DATA_CALL request
 */
static void requestDeactivateDataCall(void *data, size_t datalen, RIL_Token t)
{
   int err ,cid_value;
   const char *cid;
   PdnTable 	pdnTbl;

   cid = ((const char **)data)[0];
   cid_value=atoi(cid);
   RLOGD("requesting DeactivateDataCall to cid '%s'", cid);
   err = getPdnTable(&pdnTbl);
   if ( err <0)
   {
		RLOGE("requestDeactivateDataCall got error from getPdnTable");
		goto error;
   }
   // find the cid
   int i;
   for (i = 0; i < pdnTbl.numOfEntries,pdnTbl.pdn[i].cid != cid_value; i++);


   if (pdnTbl.pdn[i].cid != cid_value){
	   RLOGE("requestDeactivateDataCall can not find cid %d",cid_value);
	   goto error;
   }
   // use proprietary at command to de-activate pdn (Internet data) connection
   char *cmd;
   asprintf(&cmd, "AT%%PDNACT=0,%d", pdnTbl.pdn[i].sessionId);
   err = at_send_command(cmd, NULL);
   free(cmd);
   if (err != 0 )
      goto error;

   if (ifc_config(IFNAME(pdnTbl.pdn[i].sessionId),IF_STATE_DOWN)<0)
   	goto error;


	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/*
 * requestGetLastCallFailCause
 * 		support for RIL_REQUEST_LAST_CALL_FAIL_CAUSE , required for VoLTE
 */
void requestGetLastCallFailCause(void *data, size_t datalen, RIL_Token t)
{
    int err;
    ATResponse *p_response = NULL;
    int response = CALL_FAIL_ERROR_UNSPECIFIED;
    char *line;
    char *responseStr=NULL;

    /* optional return values :  "NO ERROR" ,"DENIED", "NO ANSWER","REJECTED","UNKNOWN" */
    err = at_send_command_singleline("AT%VECEER", "%VECEER:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);

    if (err < 0) {
        goto error;
    }
    err = at_tok_nextstr(&line, &responseStr);
    if (err < 0 || !responseStr) goto error;
    /* if the 1st word is NO , parse based on the next word */
    if ( strstr(responseStr,"NO")!=NULL){
       	err = at_tok_nextstr(&line, &responseStr);
        if (err < 0 || !responseStr) goto error;
    }

    if ( strstr(responseStr,"ERROR")!=NULL){
    	response=CALL_FAIL_NORMAL;
    } else if ( strstr(responseStr,"DENIED")!=NULL){
    	response=CALL_FAIL_BUSY;
    } else if ( strstr(responseStr,"ANSWER")!=NULL){
    	response=CALL_FAIL_BUSY;
    } else if ( strstr(responseStr,"REJECTED")!=NULL){
    	response=CALL_FAIL_CONGESTION;
    } else if ( strstr(responseStr,"UNKNOWN")!=NULL){
    	response=CALL_FAIL_ERROR_UNSPECIFIED;
    }


    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
    at_response_free(p_response);
    return;
error:
    at_response_free(p_response);
    RLOGE("requestGetLastCallFailCause error");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}


/*
 * requestGetLastCallFailCause
 * 		support for RIL_REQUEST_QUERY_CLIP , required for VoLTE
 */
void requestGetClip(void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *p_response = NULL;
	int response = 0;
	char *line;

	/* 3gpp27007 +CLIP?	+CLIP: <n>,<m>  */
	err = at_send_command_singleline("AT+CLIP?", "+CLIP:", &p_response);
	if (err < 0 || p_response->success == 0) goto error;
	line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0)   goto error;
    // <n>
    err = at_tok_nextint(&line, response);
    if (err < 0 ) goto error;
    // <m>
    err = at_tok_nextint(&line, response);
    if (err < 0 ) goto error;


    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
    at_response_free(p_response);
    return;
error:
    at_response_free(p_response);
    RLOGE("requestGetClip error");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}


/*
 * requestGetClir
 * 		support for RIL_REQUEST_GET_CLIR , required for VoLTE
 */
void requestGetClir(void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *p_response = NULL;
	int response[2];
	char *line;

	memset(response,0,2*sizeof(int));

	/* 3gpp27007 +CLIP?	+CLIP: <n>,<m>  */
	err = at_send_command_singleline("AT+CLIP?", "+CLIP:", &p_response);
	if (err < 0 || p_response->success == 0) goto error;
	line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0)   goto error;
    // <n>
    err = at_tok_nextint(&line, response[0]);
    if (err < 0 ) goto error;
    // <m>
    err = at_tok_nextint(&line, response[1]);
    if (err < 0 ) goto error;


    RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, 2*sizeof(int));
    at_response_free(p_response);
    return;
error:
    at_response_free(p_response);
    RLOGE("requestGetClir error");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}

/*
 * requestGetClir
 * 		support for RIL_REQUEST_SET_CLIR , required for VoLTE
 */
void requestSetClir(void *data, size_t datalen, RIL_Token t)
{
	int clir_value;
  	const char *clir;
   	char *cmd;

  	clir = ((const char **)data)[0];
	clir_value=atoi(clir);
   	asprintf(&cmd, "AT+CLIR=%d", clir_value);
   	at_send_command(cmd, NULL);
	free(cmd);
    RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}


/*
 * requestQuaryCallWaiting
 * 		support for RIL_REQUEST_QUERY_CALL_WAITING , required for VoLTE
 */
void requestQuaryCallWaiting(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int response[2] = {0, 0};
	int class = ((int *)data)[0];
	char *cmd;
	char *line;
	int err;

	/* ignore multi lines +CCWA: <status>,<class1>[<CR><LF>+CCWA: <status>,<class2>[...]]  */
	asprintf(&cmd, "AT+CCWA=1,2,%d", class);
	err = at_send_command_singleline(cmd, "+CCWA: ",&p_response);
	free(cmd);
	if (err < 0 || p_response->success == 0) goto error;

	line = p_response->p_intermediates->line;
	err = at_tok_start(&line);
    if (err < 0)   goto error;
    //<status>
	err = at_tok_nextint(&line, &response[0]);
	if (err < 0)   goto error;
	//<class1>
	err = at_tok_nextint(&line, &response[1]);
	if (err < 0)   goto error;



	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
	at_response_free(p_response);
	return;
error:
    at_response_free(p_response);
    RLOGE("requestQuaryCallWaiting error");
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}


/*
 * requestSetCallWaiting
 * 		support for RIL_REQUEST_SET_CALL_WAITING , required for VoLTE
 */
void requestSetCallWaiting(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
    int enable = ((int *)data)[0];
    int class = ((int *)data)[1];
    char *cmd;
	int err;

    asprintf(&cmd, "AT+CCWA=1,%d,%d", enable, class);
    err = at_send_command(cmd,&p_response);

    free(cmd);
    if (err < 0 || p_response->success == 0) {
        RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    } else {
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
    }
    at_response_free(p_response);
}





/*
 * getIpAddress
 * 		get teh IP address and family of interface
 * input:   if_name			- the name of the interface
 * output:  address			- the interface address
 * 			addressFamily	- the interface type
 * return: -1 on error
 */
static int getIpAddress(const char* if_name,char* address,int addressFamily)
{
    FILE *fp;

    char *linkLocalAddr;
    char ipv6Addr[70];
    char tmp1[3];
    char tmp2[3];
    char tmp3[3];
    char tmp4[3];
    char ifName[20];
    int i;
    int ret = -1;

    fp = fopen("/proc/net/if_inet6", "r");

    while (fscanf(fp,"%s %s %s %s %s %s",ipv6Addr,tmp1,tmp2,tmp3,tmp4,ifName) == 6)
    {
        RLOGD("%s %s %s %s %s %s",ipv6Addr,tmp1,tmp2,tmp3,tmp4,ifName);
        if (strcmp(ifName,if_name) == 0)
        {
            linkLocalAddr = strstr(ipv6Addr,"fe80") ;

            if (linkLocalAddr == ipv6Addr)
            {
               RLOGD("found a link local address %s skipping to global",ipv6Addr);
               // this is a link local address - we need a regular.
               continue;
            }

            for (i= 0 ; i < 32 ;i +=4)
            {
                address += sprintf(address,"%c%c%c%c:",ipv6Addr[i],ipv6Addr[i+1],ipv6Addr[i+2],ipv6Addr[i+3]);
            }
            address--;
            *address = '\0';
            ret = 0;
            break;
        }

    }


    return ret;
}

/*
 * convertToIpv6Addr
 * 		string format convert for IPv6 addresses
 * input:   ipv6Addr			- the unformated string
 * output:  formatAddr			- the formatted address
 * return: -1 on error
 */
static void convertToIpv6Addr(char* formatAddr,char* ipv6Addr)
{
    int addr[16];
    RLOGD("scanning address %s ",ipv6Addr);

    sscanf(ipv6Addr,"%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d",
           &addr[0],
           &addr[1],
           &addr[2],
           &addr[3],
           &addr[4],
           &addr[5],
           &addr[6],
           &addr[7],
           &addr[8],
           &addr[9],
           &addr[10],
           &addr[11],
           &addr[12],
           &addr[13],
           &addr[14],
           &addr[15]);

    RLOGD("converting to formal ipv6 address %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
            addr[0],
            addr[1],
            addr[2],
            addr[3],
            addr[4],
            addr[5],
            addr[6],
            addr[7],
            addr[8],
            addr[9],
            addr[10],
            addr[11],
            addr[12],
            addr[13],
            addr[14],
            addr[15]);

    sprintf(formatAddr,"%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
            addr[0],
            addr[1],
            addr[2],
            addr[3],
            addr[4],
            addr[5],
            addr[6],
            addr[7],
            addr[8],
            addr[9],
            addr[10],
            addr[11],
            addr[12],
            addr[13],
            addr[14],
            addr[15]);
}

/*
 * requestSignalStrength_helper
 * 		this is a helper function to get the %MEAS result
 * input:   type			- the measurement type needed
 * output:  formatAddr		- the values read from the modem
 * return: -1 on error
 */
static int requestSignalStrength_helper(MEAS_Type type, int* responseVal)
{
	int err=-1;
	char *line;
	ATResponse *p_response = NULL;

    // altair - use AT%MEAS to get various measurements -
    // 0 for RSRP
    // 1 for RSRQ
    // 2 for SINR
    // 3 for RSSI
	// 7 for CQI
	//13 for RS_SNR
	switch(type)
	{
	case MEAS_RSRP:
		err = at_send_command_singleline("AT%MEAS=\"0\"", "RSRP: Reported = ", &p_response);
		break;
	case MEAS_RSRQ:
		err = at_send_command_singleline("AT%MEAS=\"1\"", "RSRQ: Reported = ", &p_response);
		break;
	case MEAS_SINR:
		err = at_send_command_singleline("AT%MEAS=\"2\"", "SINR: Reported = ", &p_response);
		break;
	case MEAS_RSSI:
		err = at_send_command_singleline("AT%MEAS=\"3\"", "RSSI: Reported = ", &p_response);
		break;
    case MEAS_RSSNR:
        err = at_send_command_singleline("AT%MEAS=\"13\"", "RS_SNR: Reported = ", &p_response);
   		break;
    case MEAS_CQI:
        err = at_send_command_singleline("AT%MEAS=\"7\"", "CQI: ", &p_response);
   		break;
    case MEAS_CSQ:
        err = at_send_command_singleline("AT+CSQ", "+CSQ: ", &p_response);
   		break;
	}

	if (err < 0 || p_response->success == 0) {
		goto error;
	}

	line = p_response->p_intermediates->line;
	err = at_tok_start(&line);
	if (err < 0) goto error;


    if ((type == MEAS_CQI) || (type == MEAS_CSQ)) {
        err = at_tok_nextint(&line, responseVal);
        if (err < 0) goto error;
    }
    else
    {
        if (!sscanf(line," Reported = %d",responseVal)) goto error;

        if ((type == MEAS_RSRP) || (type == MEAS_RSRQ))  {
            // in case of RSRP or RSRQ the framework expects a positive value.
            *responseVal = *responseVal * (-1);
        }
    }

	at_response_free(p_response);
	return 0;

error:
    RLOGW("requestSignalStrength_helper %s not available" , MEAS_TYPE_STR_CONST[type]);
	at_response_free(p_response);

    // in case of an error report invalid measurement (-1)
    *responseVal = -1;
	return -1;
}


/*
 * added by altair
 * todo should be moved to channel.c
 * this command is identicle to  at_send_command_singleline , with the addition that it check for error and
 * retry again in case of error.
 * This methode is used in case the modem may be not ready, the additional delay and retry will needed.
 *
 * parameters:
 * 		numOfRetransmit - number if time to retransmit in case of error
 * output:
 * 		error in case all re transit attempts failed
 *
 */
static int at_send_command_reliable_interenal (const char *command,
                                        const char *responsePrefix,
                                        ATResponse **pp_outResponse,
                                        int numOfRetransmit,
                                        int isSingleLine)
{
	 int err = -1;
	 int retransmitCount = numOfRetransmit;
	 ATResponse *p_response = NULL;

	 for (;numOfRetransmit >= 0 ; numOfRetransmit--) 
     {
         if (isSingleLine) 
         {
            err=at_send_command_singleline(command, responsePrefix, &p_response);
         }
         else
         {
             err=at_send_command(command, &p_response);
         }
		
        if (err >= 0 && p_response->success != 0)
        {
            if (pp_outResponse) {
                *pp_outResponse = p_response;
            }
            else
            {
                at_response_free(p_response);
            }

            break;
        }

        at_response_free(p_response);
		sleep(1);
	 }
	 *pp_outResponse = p_response;
	 return err;
}

#define IF_STATE_UP 	1
#define IF_STATE_DOWN 	2
/*
 * ifc_config
 * 		set interface up/down
 * input:   char* ifName			- the interface name , can be retrieved using IFNAME(sessionid)
 * 			int ifState				- IF_STATE_UP / IF_STATE_DOWN
 * output:  none
 * return: -1 on error
 */
int ifc_config(char* ifName, int ifState )
{
	if (IF_STATE_UP == ifState ){
		if (ifc_init() < 0)
			goto error ;
		do_dhcp(ifName);
		ifc_close();
		RLOGI("ifc_config %s is up",ifName);
	}else if (IF_STATE_DOWN == ifState) {
		if (ifc_init() < 0)
			goto error ;
		ifc_down(ifName);
		ifc_close();
		RLOGI("ifc_config %s is down",ifName);
	}else goto error;

	return 0;
error:
	RLOGE("ifc_config error, ifState =%d, ifName=%s ",ifState,ifName);
	return -1;
}

int at_send_command_singleline_reliable (const char *command,
                                const char *responsePrefix,
                                 ATResponse **pp_outResponse,
                                 int numOfRetransmit)
{
    return at_send_command_reliable_interenal(command,responsePrefix,pp_outResponse,numOfRetransmit,1);
}

int at_send_command_reliable (const char *command,
				ATResponse **pp_outResponse,
                              int numOfRetransmit)
{
	 return at_send_command_reliable_interenal(command,NULL,pp_outResponse,numOfRetransmit,0);
}

int isDelayAtChannel()
{
	char  propValue[PROP_VALUE_MAX];
	// this is a temporary patch to allow slower AT channel communication
	if (__system_property_get("ril.delayAtChannel", propValue) != 0) {
		if (propValue[0]=='1'){
			RLOGI("delayAtChannel is enabled");
	        return 1;
		} else{
			return 0;
		}
	}
	return 0;
}


