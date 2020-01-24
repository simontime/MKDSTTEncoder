#ifdef _WIN32
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define MI_CpuCopy8(src, dest, size) memcpy(dest, src, size)
#define OS_GetTick() 0
#endif

#define NUM_CUPS        8
#define COURSES_PER_CUP 4

#define MSECS_PER_SEC 1000
#define MSECS_PER_MIN 1000 * 60

#define FOUR_MINUTES 4 * MSECS_PER_MIN

const int crcPoly = 0x1021;

/* Structure passed to the encoding function */
typedef struct
{
    u16 msecs;
    u8  mins;
    u8  secs;
    u16 playerName[10];
    u8  isValid;
    u8  character;
    u8  kart;
    /* Never used. Other game functions memset this struct with 32 bytes. */
    u8 padding[5];
} RaceStats;

/* Letter lookup table for custom base32 encoding. */
const char letterTable[] = "S7LCX3JZE8FG4HBKWN52YPA6RTU9VMDQ";

/* Course id table, where index of internal id = course,
 * i.e. id 20, which is at index 0 = 1st cup, 1st course: Figure-8 Circuit.
 */
const int courseIdTable[] =
{
    20, 22, 31, 18, 27, 28, 33, 24,
    30, 17, 25, 19, 34, 26, 32, 29,
    10, 11, 13, 14, 35, 16, 12,  9,
    15, 36, 37, 38, 39, 23, 40,  1
};

/* Converts an internal course id to an ordered course id. */
int GetCourseId(int inId)
{
    int outId = -1;
    
    for (int cup    = 0; cup    < NUM_CUPS;        cup++)    
    for (int course = 0; course < COURSES_PER_CUP; course++)
    {
        if (inId == courseIdTable[(cup * COURSES_PER_CUP) + course])
        {
            outId = (cup * COURSES_PER_CUP) + course;
            break;
        }
    }
    
    return outId;
}

/* An implementation of CRC16-CCITT. */
u16 CalculateCRC16CCITT(u8 *data, u32 len)
{
    u16 sum = 0;
    
    for (u32 i = 0; i < len; i++)
    {
        u8 ch = *data++;

        for (int j = 0; j < 8; j++)
        {
            if (sum & 0x8000)
                sum = (sum << 1) ^ crcPoly;
            else
                sum <<= 1;
            
            if (ch & 0x80)
                sum ^= 1;
            
            ch <<= 1;
        }
    }
    
    return sum;
}

void CalculateCodeChecksum(u8 *data, int len, int flag)
{
    u8 *dataEnd = &data[len - 4];
    
    if (!flag)
    {
        dataEnd[0] = 0;
        dataEnd[1] = 0;
    }
    
    /* Zero the last u16 of the code... a second time. */
    dataEnd[2] = 0;
    dataEnd[3] = 0;
    
    u16 crcSum = CalculateCRC16CCITT(data, len);
    
    /* I have no idea what this does. */
    if (!flag)
    {
        dataEnd[0] = (OS_GetTick() >> 16) & 0xff;
        dataEnd[1] = OS_GetTick() & 0xff;
    }
    
    /* Set the last u16 of the code to the calculated checksum. */
    dataEnd[2] = crcSum >> 8;
    dataEnd[3] = crcSum & 0xff;
}

int CalculateTimeTrialCode(char *output, RaceStats *stats, int course)
{    
    u8 code[10] = {0};
    
    /* Converts the race time to milliseconds. */
    u32 totalTime = stats->msecs + (stats->secs * MSECS_PER_SEC) + (stats->mins * MSECS_PER_MIN);
    
    /* Records 4 minutes or longer cannot be encoded. */
    if (totalTime >= FOUR_MINUTES)
        return 0;
    
    int courseId = GetCourseId(course);
    
    /* Course id must be within bounds. */
    if (courseId < 0 || courseId > 31)
        return 0;
    
    /* Quotient is the character id, remainder is the kart id. */
    u32 kartCharacter = (stats->character * 37) + stats->kart;
    
    /* Must be storable in 9 bits. */
    if (kartCharacter >= 0x200)
        return 0;
    
    /* First u32 in the code is a bitfield containing the total time and all ids. */
    *(u32 *)&code[0] = (totalTime << 14) | ((courseId % 32) << 9) | kartCharacter % 0x200;
    
    /* Next u32 in the code is the first 2 characters of the player's name, stored as UTF-16. */
    MI_CpuCopy8(stats->playerName, &code[4], 4);
    
    /* Zero the last u16 in the code for checksum calculation. */
    *(u16 *)&code[8] = 0;
    
    CalculateCodeChecksum(code, 10, 1);
    
    /* Simple XOR encryption/obfuscation of the code.
     * Calculated backwards, likely for its avalanche effect
     * making similar course records' codes look more random
     * since it XORs the checksum's bytes first.
     */
    u8 key = 0xC3;
    
    for (int i = 9; i >= 0; i--)
    {
        code[i] ^= key;
        key = code[i];
    }
    
    /* Encodes the code to a base32 string using a custom lookup table. */
    for (int i = 0, byte = 0; i < 16; i++, byte += 5)
    {
        u16 encByte = 0;
        
        for (u8 bit = 0; bit < 5; bit++)
        {
            u32 bitOffset = byte + bit;
            
            encByte |= (code[bitOffset / 8] >> (7 - (bitOffset % 8))) & 1;
            encByte <<= 1;
        }
        
        output[i] = letterTable[encByte >> 1];
    }
    
    return 1;
}

#ifdef _WIN32
/* Simple program demonstrating encoder usage. */
int main()
{
    char outcode[16];
    
    /* These stats represent the Yoshi Falls world record at time of writing. */
    RaceStats stats;
    
    /* Time: 0:45:994 */
    stats.mins  = 0;
    stats.secs  = 45;
    stats.msecs = 994;
    
    /* Character: Yoshi */
    stats.character = 6;
    
    /* Kart: ROB-BLS */
    stats.kart = 34;
    
    /* Player: MKDasher */
    wcsncpy(stats.playerName, L"MK", 2);
    
    CalculateTimeTrialCode(outcode, &stats, /* Course: Yoshi Falls */ 22);
    
    printf("Generated code: %c%c%c%c %c%c%c%c %c%c%c%c %c%c%c%c",
            outcode[0],  outcode[1],  outcode[2],  outcode[3],
            outcode[4],  outcode[5],  outcode[6],  outcode[7],
            outcode[8],  outcode[9],  outcode[10], outcode[11],
            outcode[12], outcode[13], outcode[14], outcode[15]);
    
    return 0;
}
#endif