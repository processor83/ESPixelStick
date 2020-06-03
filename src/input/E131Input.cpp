/*
* E131Input.h - Code to wrap ESPAsyncE131 for input
*
* Project: ESPixelStick - An ESP8266 and E1.31 based pixel driver
* Copyright (c) 2019 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*/

#include "E131Input.h"
#include "../ESPixelStick.h"
#include "../FileIO.h"

#include "E131Input.h"


//-----------------------------------------------------------------------------
c_InputE131::c_InputE131 (c_InputMgr::e_InputChannelIds NewInputChannelId,
                          c_InputMgr::e_InputType       NewChannelType,
                          uint8_t                     * BufferStart,
                          uint16_t                      BufferSize) :
    c_InputCommon(NewInputChannelId, NewChannelType, BufferStart, BufferSize)

{
    // DEBUG_START;
    // DEBUG_END;
} // c_InputE131

//-----------------------------------------------------------------------------
c_InputE131::~c_InputE131()
{
    if (seqTracker) { free (seqTracker); seqTracker = nullptr; }

    if (seqError)  { free (seqError); seqError = nullptr; }
}

//-----------------------------------------------------------------------------
void c_InputE131::Begin ()
{
    // DEBUG_START;
    Serial.println(F("** E1.31 Initialization **"));

    if (true == HasBeenInitialized) { return; }
    HasBeenInitialized = true;

    // Create a new ESPAsyncE131
    if (nullptr != e131) { free (e131); e131 = nullptr; }
    e131 = new ESPAsyncE131(10);
    // DEBUG_V ("");

    validateConfiguration ();
    // DEBUG_V ("");

    // Get on with business
    if (multicast) 
    {
        if (e131->begin(E131_MULTICAST, startUniverse, LastUniverse - startUniverse + 1)) 
        {
            // DEBUG_V ("");
            LOG_PORT.println(F("E1.31 Multicast Enabled."));
        }
        else
        {
            // DEBUG_V ("");
            LOG_PORT.println(F("*** E1.31 MULTICAST INIT FAILED ****"));
        }
    }
    else
    {
        // DEBUG_V ("");

        if (e131->begin(E131_UNICAST)) 
        {
            LOG_PORT.println (String(F("E1.31 Unicast port: ")) + E131_DEFAULT_PORT);
        }
        else
        {
            LOG_PORT.println(F("*** E1.31 UNICAST INIT FAILED ****"));
        }
    }

    // DEBUG_END;
} // Begin

//-----------------------------------------------------------------------------
void c_InputE131::validateConfiguration()
{
    // DEBUG_START;

    if (startUniverse < 1) { startUniverse = 1; }
       
    // DEBUG_V ("");
    if (universe_channel_limit > UNIVERSE_MAX || universe_channel_limit < 1) { universe_channel_limit = UNIVERSE_MAX; }
        
    // DEBUG_V ("");
    if (channel_start < 1)
    {
        // move to the start of the first universe
        channel_start = 1;
    }        
    else if (channel_start > universe_channel_limit)
    {
        // channel start must be within the first universe
        channel_start = universe_channel_limit;
    }

   // Find the last universe we should listen for
    // DEBUG_V ("");
    uint16_t span = channel_start + InputDataBufferSize - 1;
    if (span % universe_channel_limit)
    {
        LastUniverse = startUniverse + span / universe_channel_limit;
    }
    else
    {
        LastUniverse = startUniverse + span / universe_channel_limit - 1;
    }

    // Setup the sequence error tracker
    // DEBUG_V ("");
    uint8_t uniTotal = (LastUniverse + 1) - startUniverse;

    // DEBUG_V ("");
    if (seqTracker) { free (seqTracker); }
    // DEBUG_V ("");
    if ((seqTracker = static_cast<uint8_t *>(malloc(uniTotal)))) { memset (seqTracker, 0x00, uniTotal); }
    // DEBUG_V ("");

    if (seqError) { free (seqError); }
    // DEBUG_V ("");
    if ((seqError = static_cast<uint32_t *>(malloc(uniTotal * 4)))) { memset (seqError, 0x00, uniTotal * 4); }
    // DEBUG_V ("");

    // Zero out packet stats
    if (nullptr == e131)
    {
        // DEBUG_V ("");
        e131 = new ESPAsyncE131 (10);
    }
    // DEBUG_V ("");
    e131->stats.num_packets = 0;
    // DEBUG_V ("");

    LOG_PORT.printf("Listening for %u channels from Universe %u to %u.\n",
            InputDataBufferSize, startUniverse, LastUniverse);

    // Setup IGMP subscriptions if multicast is enabled
    if (multicast) { SubscribeToMulticastDomains (); }
    // DEBUG_END;

} // validateConfiguration

//-----------------------------------------------------------------------------
boolean c_InputE131::SetConfig (ArduinoJson::JsonObject & jsonConfig)
{
    // DEBUG_START;
    bool retval = 0;

    if (true == HasBeenInitialized)
    {
        retval = retval | FileIO::setFromJSON(startUniverse,          jsonConfig["universe"]);
        retval = retval | FileIO::setFromJSON(universe_channel_limit, jsonConfig["universe_limit"]);
        retval = retval | FileIO::setFromJSON(channel_start,          jsonConfig["channel_start"]);
        retval = retval | FileIO::setFromJSON(multicast,              jsonConfig["multicast"]);

        // DEBUG_V("");
        validateConfiguration ();
    }

    // DEBUG_END;
    return retval;
} // deserialize

//-----------------------------------------------------------------------------
void c_InputE131::GetConfig (JsonObject & jsonConfig)
{
    // DEBUG_START;

    jsonConfig["universe"]       = startUniverse;
    jsonConfig["universe_limit"] = universe_channel_limit;
    jsonConfig["channel_start"]  = channel_start;
    jsonConfig["multicast"]      = multicast;

    // DEBUG_END;

} // GetConfig

void c_InputE131::SetBufferInfo (uint8_t* BufferStart, uint16_t BufferSize)
{
    InputDataBuffer = BufferStart;
    InputDataBufferSize = BufferSize;

    // buffer has moved. Start Over
    Begin ();

} // SetBufferInfo

//-----------------------------------------------------------------------------
// Subscribe to "n" universes, starting at "universe"
void c_InputE131::SubscribeToMulticastDomains()
{
    uint8_t count = LastUniverse - startUniverse + 1;
    IPAddress ifaddr = WiFi.localIP ();
    IPAddress multicast_addr;

    for (uint8_t UniverseIndex = 0; UniverseIndex < count; ++UniverseIndex) 
    {
        multicast_addr = IPAddress (239, 255,
                                    (((startUniverse + UniverseIndex) >> 8) & 0xff),
                                    (((startUniverse + UniverseIndex) >> 0) & 0xff));

        igmp_joingroup ((ip4_addr_t*)&ifaddr[0], (ip4_addr_t*)&multicast_addr[0]);
    }
} // multiSub

//-----------------------------------------------------------------------------
void c_InputE131::Process ()
{
    uint8_t     *E131Data;
    uint8_t     uniOffset;
    uint16_t    universe;
    uint16_t    offset;
    uint16_t    dataStart;
    uint16_t    dataStop;
    uint16_t    channels;
    uint16_t    buffloc;

    // Parse a packet and update pixels
    while (!e131->isEmpty()) 
    {
        e131->pull(&packet);
        universe = htons(packet.universe);
        E131Data = packet.property_values + 1;

        //LOG_PORT.print(universe);
        //LOG_PORT.println(packet.sequence_number);

        if ((universe >= startUniverse) && (universe <= LastUniverse))
        {
            // Universe offset and sequence tracking
            uniOffset = (universe - startUniverse);
            if (packet.sequence_number != seqTracker[uniOffset]++)
            {
                LOG_PORT.print(F("Sequence Error - expected: "));
                LOG_PORT.print(seqTracker[uniOffset] - 1);
                LOG_PORT.print(F(" actual: "));
                LOG_PORT.print(packet.sequence_number);
                LOG_PORT.print(F(" universe: "));
                LOG_PORT.println(universe);
                seqError[uniOffset]++;
                seqTracker[uniOffset] = packet.sequence_number + 1;
            }

            // Offset the channels if required
            offset = channel_start - 1;

            // Find start of data based off the Universe
            dataStart = uniOffset * universe_channel_limit - offset;

            // Calculate how much data we need for this buffer
            dataStop = InputDataBufferSize;
            channels = htons(packet.property_value_count) - 1;
            if (universe_channel_limit < channels)
            {
                channels = universe_channel_limit;
            }

            if ((dataStart + channels) < dataStop)
            {
                dataStop = dataStart + channels;
            }

            // Set the data
            buffloc = 0;

            // ignore data from start of first Universe before channel_start
            if (dataStart < 0) 
            {
                dataStart = 0;
                buffloc = channel_start - 1;
            }

            for (int UniverseIndex = dataStart; UniverseIndex < dataStop; UniverseIndex++) 
            {
                InputDataBuffer[UniverseIndex] = E131Data[buffloc];
                buffloc++;
            }
        }
    }
//    LOG_PORT.printf("procJSON heap /stack stats: %u:%u:%u:%u\n", ESP.getFreeHeap(), ESP.getHeapFragmentation(), ESP.getMaxFreeBlockSize(), ESP.getFreeContStack());
} // process
