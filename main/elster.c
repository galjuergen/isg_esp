/*
 *
 *  Copyright (C) 2014 Jürg Müller, CH-5524
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see http://www.gnu.org/licenses/ .
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "elster.h"
#include "elsterTable.inc"

static const char *TAG = "TWAI";

#define High(A)     (sizeof(A)/sizeof(A[0]) - 1)

static signed short ElsterTabIndex[0x10000];

static const char * ElsterTypeStr[] =
{
  "et_default",
  "et_dec_val",
  "et_cent_val",
  "et_mil_val",
  "et_byte",
  "et_double_val",
  "et_triple_val",
  "et_little_endian",
  "et_zeit",
  "et_datum",
  "et_time_domain",
  "et_dev_nr",
  "et_err_nr"
};

void SetValueType(char * Val, uint8_t Type, uint16_t Value)
{
   if (Value == 0x8000)
     strcpy(Val, "not available");
   else
   switch (Type)
   {
     case et_byte:
       sprintf(Val, "%d", (uint8_t)Value);
       break;

     case et_dec_val:
       sprintf(Val, "%.1f", ((double)((int16_t)Value)) / 10.0);
       break;

     case et_cent_val:
       sprintf(Val, "%.2f", ((double)((int16_t)Value)) / 100.0);
       break;

     case et_mil_val:
       sprintf(Val, "%.3f", ((double)((int16_t)Value)) / 1000.0);
       break;

     case et_little_endian:
       sprintf(Val, "%d", (Value >> 8) + 256*(Value & 0xff));
       break;
       
     case et_little_bool:
       if (Value == 0x0100)
         strcpy(Val, "1");
       else
         strcpy(Val, "0");
       break;
       
     case et_bool:
       if (Value == 0x0001)
         strcpy(Val, "1");
       else
         strcpy(Val, "0");
       break;
     
     case et_betriebsart:
       if ((Value & 0xff) == 0 && (Value >> 8) <= (int) High(BetriebsartList))
         strcpy(Val, BetriebsartList[Value >> 8].Name);
       else
         strcpy(Val, "?");
       break;

     case et_zeit:
       sprintf(Val, "%2.2d:%2.2d", Value & 0xff, Value >> 8);
       break;

     case et_datum:
       sprintf(Val, "%2.2d.%2.2d.", Value >> 8, Value & 0xff);
       break;

     case et_time_domain:
       if (Value & 0x8080)
         strcpy(Val, "not used time domain");
       else
         sprintf(Val, "%2.2d:%2.2d-%2.2d:%2.2d",
                 (Value >> 8) / 4, 15*((Value >> 8) % 4),
                 (Value & 0xff) / 4, 15*(Value % 4));
       break;

     case et_dev_nr:
       if (Value >= 0x80)
         strcpy(Val, "--");
       else
         sprintf(Val, "%d", Value + 1);
       break;

     case et_dev_id:
       sprintf(Val, "%d-%2.2d", (Value >> 8), Value & 0xff);
       break;

     case et_err_nr:
     {
       int idx = -1;
       for (unsigned i = 0; i <= High(ErrorList); i++)
         if (ErrorList[i].Index == Value)
         {
           idx = i;
           break;
         }
       if (idx >= 0)
         strcpy(Val, ErrorList[idx].Name);
       else
         sprintf(Val, "ERR %d", Value);
       break;
     }

     case et_default:
     default:
       sprintf(Val, "%d", (signed short)Value);
       break;
   }
}

void SetDoubleType(char * Val, unsigned char Type, double Value)
{
  switch (Type)
  {
    case et_double_val:
      sprintf(Val, "%.3f", Value);
      break;

    case et_triple_val:
      sprintf(Val, "%.6f", Value);
      break;

    default:
      sprintf(Val, "%g", Value);
      break;
  }
}

int GetElsterTableIndex(uint16_t Index)
{
  return ElsterTabIndex[Index];
}

const char * GetElsterTableName(uint16_t Index)
{
  int ind = GetElsterTableIndex(Index);
  if (ind >= 0)
    return ElsterTable[ind].Name;
  else
    return "";
}

ElsterValueType GetElsterType(const char * str)
{
  if (str)
  {
    for (unsigned i = 0; i <= High(ElsterTypeStr); i++)
      if (!strcmp(ElsterTypeStr[i], str))
        return (ElsterValueType) i;
  }
  return et_default;
}

const char * ElsterTypeToName(ElsterValueType Type)
{
  if (Type <= High(ElsterTypeStr))
    return ElsterTypeStr[Type];

  return ElsterTypeStr[et_default];
}

const ElsterIndex * GetElsterIndex(uint16_t Index)
{
  int Ind = GetElsterTableIndex(Index);

  return Ind >= 0 ? &ElsterTable[Ind] : NULL;
}

// bool FormElsterTable(const KCanFrame & Frame, char * str)
// {
//   strcpy(str, "?");

//   if (Frame.Len != 7)
//     return false;

//   int Index = Frame.GetElsterIdx();
//   if (Index < 0)
//     return false;

//   int ind = GetElsterTableIndex((unsigned short)Index);
//   int Value = Frame.GetValue();
//   if (ind < 0 || Value < 0)
//     return false;

//   char val[64];

//   SetValueType(val, ElsterTable[ind].Type, (unsigned short) Value);
//   sprintf(str, "%s = %s", ElsterTable[ind].Name, val);

//   return true;
// }

const ElsterIndex * GetElsterIndexFromString(const char * str)
{
  for (int i = 0; i <= (int) High(ElsterTable); i++)
    if (!strcmp(str, ElsterTable[i].Name))
      return &ElsterTable[i];

  return NULL;
}

uint16_t getElsterReceiver(uint8_t length, uint8_t const * const data)
{
  if (length < 2 || data[1] == 0x79)
    return 0xFF;

  return (((uint16_t)(data[0] & 0xf0)) << 3) | (uint16_t)(data[1] & 0x7f);
}

void setElsterReceiver(uint8_t length, uint8_t * data, uint16_t receiver)
{
  if (length < 2)
    return;
  
  data[0] |= (receiver >> 3) & 0xf0;
  data[1] |= receiver & 0x7f;
}

ElsterPacketType getElsterPacketType(uint8_t length, uint8_t const * const data)
{
  if (length > 0 && (data[0] & 0x0f) < ELSTER_PT_invalid)
  {
    return (ElsterPacketType)(data[0] & 0x0f);
  }
  return ELSTER_PT_invalid;
}

void setElsterPacketType(uint8_t length, uint8_t * data, ElsterPacketType packetType)
{
  if (length < 1)
    return;

  if (packetType > ELSTER_PT_invalid)
    packetType = ELSTER_PT_invalid;
  
  data[0] |= ((uint8_t)packetType) & 0x0f;
}

uint16_t getElsterIndex(uint8_t length, uint8_t const * const data)
{
  if (length > 7 || length < 3)
    return 0xFF;

  if (data[2] == 0xfa)
  {
    if (length < 5)
      return 0xFF;
    
    return ((uint16_t)(data[3])<<8) | (uint16_t)(data[4]);
  }

  return (uint16_t)data[2];
}

void setElsterIndex(uint8_t length, uint8_t * data, uint16_t index)
{
  if (length > 7 || length < 3)
    return;

  if (index < 0xfa)
  {
    data[2] = (uint8_t)index;
  }
  else
  {
    data[2] = 0xfa;
    data[3] = (uint8_t)(index >> 8);
    data[4] = (uint8_t)(index & 0xff);
  }
}

uint16_t getElsterRawValue(uint8_t length, uint8_t const * const data)
{
  if (length < 5 || length > 7)
    return 0xFFFF;
  
  if (data[2] == 0xfa)
  {
    if (length != 7)
      return 0xFFFF;
    
    return ((uint16_t)data[5] << 8) | (uint16_t)(data[6] & 0xFF);
  }
  
    return ((uint16_t)data[3] << 8) | (uint16_t)(data[4] & 0xFF);
}

ElsterPacketReceive ElsterRawToReceivePacket(uint16_t sender, uint8_t length, uint8_t const * const data)
{
  ElsterPacketReceive p;
  if (length != 7)
  {
    ESP_LOGE(TAG,"ParseElster failed: invalid length");
    p.packetType = ELSTER_PT_invalid;
    return p;
  }

  p.sender = sender;
  p.receiver = getElsterReceiver(length, data);
  p.packetType = getElsterPacketType(length, data);
  p.index = getElsterIndex(length, data);
  uint16_t rawValue = getElsterRawValue(length, data);

  for (uint16_t tableIndex = 0; tableIndex <= High(ElsterTable); tableIndex++)
  {
      if (ElsterTable[tableIndex].Index == p.index)
      {
          p.valueType = ElsterTable[tableIndex].Type;
          SetValueType(p.value, p.valueType, rawValue);
          strncpy(p.indexName, ElsterTable[tableIndex].Name, sizeof(p.indexName));
          ESP_LOGI(TAG, "sender: 0x%x, receiver: 0x%x, type: 0x%x, index: 0x%04x, %d (%s), val: %s", p.sender, p.receiver, p.packetType, p.index, tableIndex, p.indexName, p.value);
          break;
      }
  }

  return p;
}

void ElsterPrepareSendPacket(uint8_t length, uint8_t * const data, ElsterPacketSend packet)
{
  if (length != 7 || data == NULL)
    return;

  memset(data, 0, length);
  setElsterReceiver(length, data, packet.receiver);
  setElsterPacketType(length, data, ELSTER_PT_READ);
  setElsterIndex(length, data, packet.index);
}

// static bool Get_Time(const char * & str, int & hour, int & min)
// {
//   TInt64 h, m;

//   if (!NUtils::GetInt(str, h))
//     return false;
//   if (*str != ':')
//     return false;
//   str++;
//   if (!NUtils::GetInt(str, m))
//     return false;

//   if (h == 24 && m)
//     return false;
    
//   if (0 <= h && h <= 24 && 0 <= m && m < 60)
//   {
//     hour = (int) h;
//     min = (int) m;

//     return true;
//   }
//   return false;
// }

// int TranslateString(const char * & str, unsigned char elster_type)
// {
//   while (*str == ' ')
//     str++;
    
//   switch (elster_type)
//   {
//     case et_default:
//     case et_byte:
//     case et_little_endian:
//     {
//       TInt64 i;

//       if (!NUtils::GetInt(str, i))
//         break;

//       if (-0x7fff <= i && i <= 0xffff)
//       {
//         unsigned short s = (unsigned short) i;
//         if (elster_type == et_byte && s > 0xff)
//           break;
//         if (elster_type == et_little_endian)
//           s = (unsigned short)((s << 8) + (s >> 8));

//         return s;
//       }
//       break;
//     }
//     case et_little_bool:
//     case et_bool:
//     {
//       int res = 0;
//       if (!strncmp(str, "on", 2))
//       {
//         str += 2;
//         res = 1;
//       } else
//       if (!strncmp(str, "off", 3))
//       {
//         str += 3;
//       } else
//         break;
      
//       if (elster_type == et_little_bool)
//         res <<= 8;
      
//       return res;
//     }
//     case et_betriebsart:
//     {
//       int s = High(BetriebsartList);
//       for ( ; s >= 0; s--)
//         if (!strncmp(BetriebsartList[s].Name, str, strlen(BetriebsartList[s].Name)))
//           break;
      
//       if (s >= 0)
//       {
//         str += strlen(BetriebsartList[s].Name);
        
//         return BetriebsartList[s].Index;
//       }
//       break;
//     }
//     case et_dec_val:  // Auflösung: xx.x / auch neg. Werte sind möglich
//     case et_cent_val: // x.xx
//     case et_mil_val:  // x.xxx
//     {
//       double d;

//       if (!NUtils::GetDouble(str, d))
//         break;

//       d *= 10.0;
//       if (elster_type == et_cent_val)
//         d *= 10.0;
//       if (elster_type == et_mil_val)
//         d *= 100.0;
//       if (-0x7fff <= d && d <= 0x7fff)
//         return (unsigned short) (int) d;
//       break;
//     }  
//     case et_zeit:
//     {
//       int hour, min;

//       if (!Get_Time(str, hour, min))
//         break;

//       if (hour < 24)
//         return (unsigned short)((min << 8) + hour);
//       break;
//     }
//     case et_datum:
//     {
//       TInt64 d, m;

//       if (!NUtils::GetInt(str, d))
//         break;
//       if (*str != '.')
//         break;
//       str++;
//       if (!NUtils::GetInt(str, m))
//         break;
//       if (*str != '.')
//         break;
//       str++;
//       if (1 <= d && d <= 31 && 1 <= m && m <= 12)
//       {
//         if (m == 2 && d >= 29)
//           break;
//         if ((m == 4 || m == 6 || m == 9 || m == 11) && d > 30)
//           break;

//         return (unsigned short)((d << 8) + m);
//       }
//       break;
//     }
//     case et_time_domain:
//     {
//       if (!*str)
//         return 0x8080; // not used time domain

//       int hour1, hour2, min1, min2;

//       if (!Get_Time(str, hour1, min1))
//         break;
//       if (*str != '-')
//         break;
//       str++;
//       if (!Get_Time(str, hour2, min2))
//         break;

//       hour1 = 4*hour1 + min1/15;
//       hour2 = 4*hour2 + min2/15;
//       if (hour1 < hour2)
//         return (unsigned short)((hour1 << 8) + hour2);
//       break;
//     }
//     case et_dev_nr:
//     case et_err_nr:
//     case et_dev_id:
//     case et_double_val:
//     case et_triple_val:
//     default:
//       break;
//   }
//   return -1;
// }

