//***************************************************************************
// File channel.hpp
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / RCON protocol implementation
//***************************************************************************

#ifndef __CHANNEL_HPP__
#define __CHANNEL_HPP__

#include "def.h"

#define RC_PID      42
#define RC_COMMAND  2
#define RC_AUTH_RESPONSE 2
#define RC_AUTHENTICATE  3
#define BUFFSIZE_DEF 10240
#define BUFFSIZE_MAX 1024*1024*10

//***************************************************************************
// struct RConPacket
//***************************************************************************

class RConPacket 
{
   public:

      RConPacket();
      ~RConPacket();

      int size;
      int id;
      int cmd;
      char* data;

      void clear();
      int resize(int newSize);

      char* getBuffer() { return data; }
      int getSize() { return packetSize; }

   protected:

      int packetSize;
};

//***************************************************************************
// class RConChannel
//***************************************************************************

class RConChannel
{
   public:

      RConChannel();
      ~RConChannel();

      int connect(const char* host, int port, const char* pass);
      int disconnect();

      int sendCommand(const char* command);
      char* getBuffer() { return thePacket.getBuffer(); }

   protected:

      int rsock; /* rcon socket */

      RConPacket thePacket;

   protected:

      // tcp/protocol functions

      int send(int id, int cmd, const char* commandString);
      int _send(char* buffer, int len);
      int receive();
      int _receive(char* buffer, int bufSize, int len= na);
      int authenticate(const char *passwd);
      int flushLine();
};


//***************************************************************************

#endif // __CHANNEL_HPP__
