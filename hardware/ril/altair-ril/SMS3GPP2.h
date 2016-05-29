/*  ---------------------------------------------------------------------------

    Copyright (C) 2014 Altair Semiconductor Ltd.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as published by
    the Free Software Foundation, see <http://www.gnu.org/licenses/>.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

  --------------------------------------------------------------------------- */

#ifndef _SMS_3GPP2_H_
#define _SMS_3GPP2_H_

#define SMS_3GPP2_BUFFER_SIZE 1560

typedef signed   char   S8;
typedef unsigned char   U8;
typedef signed   short  S16;
typedef unsigned short  U16;


typedef signed int S32;
typedef unsigned int U32;

typedef enum
{
	SMS3GPP2TransMsgType_PointToPoint = 0,
	SMS3GPP2TransMsgType_Broadcast = 1,
	SMS3GPP2TransMsgType_Acknowledge = 2

}SMS3GPP2TransMsgType_e;

typedef enum 
{
    SMS3GPP2TransTag_TeleserviceIdentifier = 0,
    SMS3GPP2TransTag_ServiceCategory =1,
    SMS3GPP2TransTag_OriginatingAddress = 2,
    SMS3GPP2TransTag_OriginatingSubaddress = 3,
    SMS3GPP2TransTag_DestinationAddress = 4,
    SMS3GPP2TransTag_DestinationSubaddress = 5,
    SMS3GPP2TransTag_BearerReplyOption = 6,
    SMS3GPP2TransTag_CauseCodes = 7,
    SMS3GPP2TransTag_BearerData = 8
       
}SMS3GPP2TransTag_e;

typedef enum
{
    SMS3GPP2Address_DigitMode_4BitDTMF = 0,
    SMS3GPP2Address_DigitMode_8BitDTMF = 1
}SMS3GPP2Address_DigitMode_e;

typedef enum
{
    SMS3GPP2Address_NumberMode_ANSIT1607 =0,
    SMS3GPP2Address_NumberMode_DataNetworkAddres = 1
}SMS3GPP2Address_NumberMode_e;



#define MAX_NUM_OF_ADDRESS_FIELDS 20

typedef struct
{
    SMS3GPP2Address_DigitMode_e digitMode;
    SMS3GPP2Address_NumberMode_e numberMode;
    U8 numOfFields;
    U8 address[MAX_NUM_OF_ADDRESS_FIELDS]; // use DTMF4Bit_digits.
}SMS3GPP2Address_t;

typedef struct
{
    U8 reply_seq;
} SMS3GPP2TransBearerReplyOption_t;

typedef enum
{
    SMS3GPP2CauseCodes_ErrorClass_no_error =0,
    SMS3GPP2CauseCodes_ErrorClass_permanent_condition =1,
    SMS3GPP2CauseCodes_ErrorClass_temporary_condition =2
}SMS3GPP2CauseCodes_ErrorClass_e;

typedef struct
{
    U8 reply_seq;
    SMS3GPP2CauseCodes_ErrorClass_e error_class;
    U8 cause_code; 
} SMS3GPP2TransCauseCodes_t;

typedef struct
{
	SMS3GPP2TransMsgType_e msgType;

#define SMS3GPP2TransTeleseviceIndetifier_CDMACellularMessagingTeleservice (4098)
#define SMS3GPP2TransTeleseviceIndetifier_CDMAWirelessApplicationProtocolWAP (4100)
    U16 teleserviceIndetifier;
    SMS3GPP2Address_t originatingAddress;
    SMS3GPP2Address_t destinationAddress;

    U32 bearerDataLen;
    U8 bearerData[255];

#define SMS3GPP2TransOpt_BearerReplyOption 0x00000001
#define SMS3GPP2TransOpt_BearerDataOption  0x00000002
#define SMS3GPP2TransOpt_CauseCodesOption 0x00000004
    U32 optinalFieldsBitmap;
    
    SMS3GPP2TransBearerReplyOption_t bearerReplyOption;

    SMS3GPP2TransCauseCodes_t causeCodes;

}SMS3GPP2TransportLayer_t;

void SMS3GPP2_TransportLayer_Free(SMS3GPP2TransportLayer_t* smsTransLayer);

SMS3GPP2TransportLayer_t* SMS3GPP2_TransportLayer_Deserialize(U8* data, U32 len);

int SMS3GPP2_TransportLayer_Serialize(SMS3GPP2TransportLayer_t* smsTrasLayer ,U8* buffer, U32* bufferLen);

SMS3GPP2TransportLayer_t* SMS3GPP2_CreateStandardMoAcknowledgeSMS(char* destination,U8 replySeq,SMS3GPP2CauseCodes_ErrorClass_e errorClass, U8 causeCode);

SMS3GPP2TransportLayer_t* SMS3GPP2_TransportLayer_copy(SMS3GPP2TransportLayer_t* smsTrasLayer);
#endif //_SMS_3GPP2_H_
