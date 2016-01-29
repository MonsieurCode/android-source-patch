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

#include "SMS3GPP2.h"
#include "stdlib.h"
#include <string.h> 
#include <assert.h>

#define LOG_TAG "RIL"
#include <utils/Log.h>

//#define MASK(start,end) ((( 1 << ((end)-(start)+1) ) - 1) << (start))
/*
 * from Table 2.7.1.3.2.4-4. Representation of DTMF Digits
 * 3GPP2 C.S0005-C (IS-2000 aka cdma2000)
 */
static char DTMF4Bit_digits[] = {
  /*  0   1   2   3   4   5   6   7   8   9   a   b   c   d   e */
     '?','1','2','3','4','5','6','7','8','9','0','*','#','?','?'
};

static inline U8* getNextTLV(U8* data,U8* tag,U8* fieldLen)
{
    *tag = *data;
    data++;
    *fieldLen = *data;
    data++;
    return data;
}

static inline U8* getDec(U8* dec, U8* data)
{
    *dec = ((*data >> 4) & 0xf) * 10;
    *dec += *data & 0xf;

    data++;
    return data;
}

static inline U8* putDec(U8 dec, U8* buffer,U32* bufferLen)
{
    *buffer = (((dec / 10) & 0xf) << 8) | ((dec % 10) & 0xf);

    (*bufferLen)--;
    return buffer;
}

static inline U8* putU16(U16 u16, U8* buffer,U32* bufferLen)
{
    *buffer = (u16 >> 8) & 0xff;
    buffer++;
    *buffer = u16 & 0xff;
    buffer++;

    *bufferLen -=2;
    return buffer;
}

static inline U8* putU32(U32 u32, U8* buffer,U32* bufferLen)
{

    *buffer = (u32 >> 24) & 0xff;
    buffer++;
    *buffer = (u32 >> 16) & 0xff;
    buffer++;
    *buffer = (u32 >> 8) & 0xff;
    buffer++;
    *buffer = u32 & 0xff;
    buffer++;

    *bufferLen -=4;
    return buffer;
}

static inline U8* getU16(U16* u16, U8* data)
{
    U8 byte1,byte2;
    byte1 = *data;
    data++;
    byte2 = *data;
    data++;

    *u16 = (byte1 << 8) | (byte2);
    return data;
}

static inline U8* getU32(U32* u32, U8* data)
{
    U8 byte1,byte2,byte3,byte4;
    byte1 = *data;
    data++;
    byte2 = *data;
    data++;
    byte3 = *data;
    data++;
    byte4 = *data;
    data++;

    *u32 = (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | (byte4);
    return data;
}

static U8* SMS3GPP2_TransportLayer_Addr_Deserialize(SMS3GPP2Address_t* smsAddr,U8* data, U8 len)
{
    U8 byte1,byte2;
    U8 numOfFields;
    U32 digitPos = 0;
    
    byte1 = *data;
    data++;
    byte2 = *data;

    smsAddr->digitMode = (SMS3GPP2Address_DigitMode_e)(byte1 >> 7) & 0x1;
    smsAddr->numberMode = (SMS3GPP2Address_NumberMode_e)(byte1 >> 6) & 0x1;
    numOfFields = smsAddr->numOfFields = ((byte1 & 0x3f) << 2) | ((byte2 >> 6) & 0x3);

    assert(smsAddr->digitMode == SMS3GPP2Address_DigitMode_4BitDTMF);//first phase supports only 4ibt - TBD 8bit

    if (smsAddr->digitMode == SMS3GPP2Address_DigitMode_4BitDTMF)
    {
        while(numOfFields > 0)
        {
            U8 firstDigitIdx = (*data >> 2) & 0xf;
            U8 secondDigitIdx = (*data & 0x3) << 2;

            numOfFields--;

            smsAddr->address[digitPos] = firstDigitIdx;
            digitPos++;
            
            if(numOfFields == 0)
            {
                break;
            }

            data++;
            secondDigitIdx |= (*data >> 6) & 0x3;
            smsAddr->address[digitPos] = secondDigitIdx;
            digitPos++;
            numOfFields--;
        }
    }
    smsAddr->address[digitPos] = 0;

    data++;

    return data;
    
}

static U8* SMS3GPP2_TransportLayer_MsgInd_Deserialize(U16* teleInd,U8* data, U8 len)
{
    assert(len == 2);

    data = getU16(teleInd,data);
    return data;
}

void SMS3GPP2_TransportLayer_Free(SMS3GPP2TransportLayer_t* smsTransLayer)
{
    if (smsTransLayer == NULL)
    {
        return;
    }

    free(smsTransLayer);
    return;
}



SMS3GPP2TransportLayer_t* SMS3GPP2_TransportLayer_Deserialize(U8* data, U32 len)
{

    U8* endPtr = data + len;
    U8 tag;
    U8 fieldLen;
    SMS3GPP2TransportLayer_t* smsTransLayer = NULL;

    assert(len > 0);

    smsTransLayer = malloc(sizeof(SMS3GPP2TransportLayer_t));

    assert(smsTransLayer);

    memset(smsTransLayer,0,sizeof(SMS3GPP2TransportLayer_t));

    smsTransLayer->msgType = (SMS3GPP2TransMsgType_e)*data;
    data++;

    while(data < endPtr)
    {
        
        SMS3GPP2TransTag_e TransTag;

        data = getNextTLV(data,&tag,&fieldLen);

        TransTag = tag;

        //ALOGD("%s: parsing tag %d \n", __FUNCTION__, TransTag);
        switch (TransTag)    
        {
        case SMS3GPP2TransTag_TeleserviceIdentifier:
            data = SMS3GPP2_TransportLayer_MsgInd_Deserialize(&smsTransLayer->teleserviceIndetifier,data,fieldLen);
            break;
        case SMS3GPP2TransTag_OriginatingAddress:
            data = SMS3GPP2_TransportLayer_Addr_Deserialize(&smsTransLayer->originatingAddress,data,fieldLen);
            break;
        case SMS3GPP2TransTag_DestinationAddress:
            data = SMS3GPP2_TransportLayer_Addr_Deserialize(&smsTransLayer->destinationAddress,data,fieldLen);
            break;
        case SMS3GPP2TransTag_BearerData:
            smsTransLayer->optinalFieldsBitmap |= SMS3GPP2TransOpt_BearerDataOption;
            smsTransLayer->bearerDataLen = fieldLen;
            memcpy(smsTransLayer->bearerData,data,fieldLen);
            data += fieldLen;
            break;
        case SMS3GPP2TransTag_CauseCodes:
            smsTransLayer->causeCodes.reply_seq = (*data >> 2) & 0x3f;
            smsTransLayer->causeCodes.error_class = (SMS3GPP2CauseCodes_ErrorClass_e)(*data & 0x03);
            data++;
            if (smsTransLayer->causeCodes.error_class != SMS3GPP2CauseCodes_ErrorClass_no_error)
            {
                smsTransLayer->causeCodes.cause_code = *data;
                data++;
            }
            break;
        case SMS3GPP2TransTag_BearerReplyOption:
            smsTransLayer->optinalFieldsBitmap |= SMS3GPP2TransOpt_BearerReplyOption;
            smsTransLayer->bearerReplyOption.reply_seq = (*data >> 2) & 0x3f;
            data++;
            break;

        default:
            ALOGD("%s: found unsupported tag %d \n", __FUNCTION__, TransTag);
            data += fieldLen;
            break;

        }
    }

    return smsTransLayer;
}

inline static U8 SMS3GPP2_char2DTMF4BitIdx(char digit)
{
    switch(digit)
    {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return digit & 0xf;
    case '0':
        return 10;
    case '*':
        return 11;
    case '#':
        return 12;
    }

    return 0;
}

static U8* SMS3GPP2_TransportLayer_Addr_Serialize(U8 addrTypeTag,SMS3GPP2Address_t* smsAddr,U8* buffer, U32* bufferLen)
{
    U8* lenPtr;
    U8 numOfFields = smsAddr->numOfFields;
    U32 digitPos;
    *buffer = (U8)addrTypeTag; // T
    buffer++;
    lenPtr = buffer; //L

    buffer++;

    // V

    *buffer = ((U8)smsAddr->digitMode & 0x1) << 7;
    *buffer |= ((U8)smsAddr->numberMode & 0x1) << 6;

    *buffer |= ((U8)smsAddr->numOfFields >> 2 ) & 0x3f;

    buffer++;

    
    *buffer = ((U8)smsAddr->numOfFields & 0x3 ) << 6;

    assert(smsAddr->digitMode == SMS3GPP2Address_DigitMode_4BitDTMF);//first phase supports only 4ibt - TBD 8bit

    if (smsAddr->digitMode == SMS3GPP2Address_DigitMode_4BitDTMF)
    {
        for (digitPos =0; digitPos < numOfFields; digitPos++)
        {

            U8 digitIdx = smsAddr->address[digitPos];
            if (digitPos % 2 == 0)
            {
                *buffer |= digitIdx << 2;
            }
            else
            {
                *buffer |= (digitIdx >> 2) & 0x3;
                buffer++;
                *buffer = (digitIdx & 0x3) << 6;
            }
        }
    }

    buffer++;

    *lenPtr = (buffer - lenPtr) - 1;

    *bufferLen -= *lenPtr +2;
    assert(*bufferLen >= 0);
    return buffer;
}

static U8* SMS3GPP2_TransportLayer_MsgInd_Serialize(U16* teleInd,U8* buffer, U32* bufferLen)
{
    *buffer = (U8)SMS3GPP2TransTag_TeleserviceIdentifier; // T
    buffer++;
    *buffer = 2; // L
    buffer++;

    *bufferLen -= 3;

    // V
    buffer = putU16(*teleInd,buffer,bufferLen);

    assert(*bufferLen >= 0);
    return buffer;
}

SMS3GPP2TransportLayer_t* SMS3GPP2_CreateStandardMoAcknowledgeSMS(char* destination,U8 replySeq,SMS3GPP2CauseCodes_ErrorClass_e errorClass, U8 causeCode)
{
    SMS3GPP2TransportLayer_t smsTransLayer;

    smsTransLayer.msgType = SMS3GPP2TransMsgType_Acknowledge;
    smsTransLayer.teleserviceIndetifier = SMS3GPP2TransTeleseviceIndetifier_CDMACellularMessagingTeleservice;
    smsTransLayer.destinationAddress.digitMode = SMS3GPP2Address_DigitMode_4BitDTMF ;
    smsTransLayer.destinationAddress.numberMode = SMS3GPP2Address_NumberMode_ANSIT1607;
    smsTransLayer.destinationAddress.numOfFields = strlen(destination);
    strncpy(smsTransLayer.destinationAddress.address,destination,MAX_NUM_OF_ADDRESS_FIELDS);

    smsTransLayer.optinalFieldsBitmap = SMS3GPP2TransOpt_CauseCodesOption;
    smsTransLayer.causeCodes.reply_seq = replySeq;
    smsTransLayer.causeCodes.error_class = errorClass;
    if (errorClass != SMS3GPP2CauseCodes_ErrorClass_no_error)
    {
        smsTransLayer.causeCodes.cause_code = causeCode;
    }

    return SMS3GPP2_TransportLayer_copy(&smsTransLayer);
}


SMS3GPP2TransportLayer_t* SMS3GPP2_TransportLayer_copy(SMS3GPP2TransportLayer_t* smsTransLayer)
{
    SMS3GPP2TransportLayer_t* smsTransLayerCpy = malloc(sizeof(SMS3GPP2TransportLayer_t));
    
    assert(smsTransLayerCpy);

    memcpy(smsTransLayerCpy,smsTransLayer, sizeof(SMS3GPP2TransportLayer_t));

    if (smsTransLayerCpy->optinalFieldsBitmap & SMS3GPP2TransOpt_BearerDataOption)
    {
        memcpy(smsTransLayerCpy->bearerData, smsTransLayer->bearerData , smsTransLayer->bearerDataLen);
    }
    return smsTransLayerCpy;
}
/*00 rpoint to point
00 - teleservier id
02 - len
1002 - 4098
04 - addrss type
07 - len
0291A55E619CC0*/

int SMS3GPP2_TransportLayer_Serialize(SMS3GPP2TransportLayer_t* smsTrasLayer ,U8* buffer, U32* bufferLen)
{
    U8* bufferStart = buffer;
    *buffer = (U8)smsTrasLayer->msgType;
    (*bufferLen)--;
    buffer++;
    
    buffer = SMS3GPP2_TransportLayer_MsgInd_Serialize(&smsTrasLayer->teleserviceIndetifier,buffer,bufferLen);

    buffer = SMS3GPP2_TransportLayer_Addr_Serialize(SMS3GPP2TransTag_DestinationAddress,&smsTrasLayer->destinationAddress,buffer,bufferLen);

    if (smsTrasLayer->optinalFieldsBitmap & SMS3GPP2TransOpt_BearerReplyOption)
    {
        *buffer = (U8)SMS3GPP2TransTag_BearerReplyOption;
        buffer++;
        *buffer = 1;
        buffer++;
        *buffer = (smsTrasLayer->bearerReplyOption.reply_seq & 0x3f) << 2;
        buffer++;
        
        *bufferLen -= 3;
        assert(*bufferLen >= 0);
    }

    if (smsTrasLayer->optinalFieldsBitmap & SMS3GPP2TransOpt_CauseCodesOption)
    {
        *buffer = (U8)SMS3GPP2TransTag_BearerReplyOption;
        buffer++;
        *buffer = (smsTrasLayer->causeCodes.error_class == SMS3GPP2CauseCodes_ErrorClass_no_error)? 1:2;
        buffer++;
        *buffer = (smsTrasLayer->causeCodes.reply_seq & 0x3f) << 2 | (smsTrasLayer->causeCodes.error_class & 0x3);
        buffer++;
        
        if (smsTrasLayer->causeCodes.error_class == SMS3GPP2CauseCodes_ErrorClass_no_error)
        {
            *bufferLen -= 3;
        }
        else
        {
             *buffer = smsTrasLayer->causeCodes.cause_code;
             buffer++;
            *bufferLen -= 4;
        }
        
        assert(*bufferLen >= 0);
    }

    if (smsTrasLayer->optinalFieldsBitmap & SMS3GPP2TransOpt_BearerDataOption)
    {
        *buffer = (U8)SMS3GPP2TransTag_BearerData;
        buffer++;
        *buffer = smsTrasLayer->bearerDataLen;
        buffer++;

        memcpy(buffer,smsTrasLayer->bearerData,smsTrasLayer->bearerDataLen);
        *bufferLen -= smsTrasLayer->bearerDataLen;

        buffer += smsTrasLayer->bearerDataLen;
        assert(*bufferLen >= 0);
    }

    *bufferLen = buffer - bufferStart;

    return 0;
}


