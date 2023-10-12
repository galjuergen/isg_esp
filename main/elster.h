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

#if !defined(KElsterTable_H)

#define KElsterTable_H

#include <stdint.h>

typedef enum
{
  // Die Reihenfolge muss mit ElsterTypeStr übereinstimmen!
  et_default = 0,
  et_dec_val,       // Auflösung: xx.x / auch neg. Werte sind möglich
  et_cent_val,      // x.xx
  et_mil_val,       // x.xxx
  et_byte,
  et_bool,          // 0x0000 und 0x0001
  et_little_bool,   // 0x0000 und 0x0100
  et_double_val,
  et_triple_val,
  et_little_endian,
  et_betriebsart,
  et_zeit,
  et_datum,
  et_time_domain,
  et_dev_nr,
  et_err_nr,
  et_dev_id
} ElsterValueType;

typedef struct
{
  uint16_t Index;
  const char * Name;
} ErrorIndex;

typedef struct
{
  const char * Name;
  uint16_t Index;
  ElsterValueType Type;
} ElsterIndex;


typedef enum
{
  ELSTER_PT_WRITE = 0,
  ELSTER_PT_READ = 1,
  ELSTER_PT_RESPONSE = 2,
  ELSTER_PT_ACK = 3,
  ELSTER_PT_WRITE_ACK = 4,
  ELSTER_PT_WRITE_RESPONSE = 5,
  ELSTER_PT_SYSTEM = 6,
  ELSTER_PT_SYSTEM_RESPONSE = 7,
  ELSTER_PT_invalid = 8
} ElsterPacketType;

typedef struct
{
  uint16_t sender;
  uint16_t receiver;
  ElsterPacketType packetType;
  ElsterValueType valueType;
  uint16_t index;
  char indexName[64];
  char value[64];
} ElsterPacketReceive;

typedef struct
{
  uint16_t receiver;
  ElsterPacketType packetType;
  uint16_t index;
} ElsterPacketSend;

const ElsterIndex * GetElsterIndex(uint16_t Index);
const ElsterIndex * GetElsterIndexFromString(const char * str);
ElsterValueType GetElsterType(const char * str);
void SetValueType(char * Val, uint8_t Type, uint16_t Value);
void SetDoubleType(char * Val, unsigned char Type, double Value);
// bool FormElsterTable(const KCanFrame & Frame, char * str);
const char * ElsterTypeToName(ElsterValueType Type);
// int TranslateString(const char * & str, unsigned char elster_type);

ElsterPacketReceive ElsterRawToReceivePacket(uint16_t sender, uint8_t length, uint8_t const * const data);

void ElsterPrepareSendPacket(uint8_t length, uint8_t * const data, ElsterPacketSend packet);

#endif

