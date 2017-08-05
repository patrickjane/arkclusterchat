//***************************************************************************
// File channel.cc
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / mcrcon implementation (by Tiiffi)
//***************************************************************************

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include "channel.hpp"

//***************************************************************************
// class RConChannel
//***************************************************************************
// ctor/dtor
//***************************************************************************

RConChannel::RConChannel()
{
   rsock= na;
}

RConChannel::~RConChannel()
{
   disconnect();
}

//***************************************************************************
// connect
//***************************************************************************

int RConChannel::connect(const char* host, int port, const char* pass)
{
   int res= success;
   char tmp[30];
   struct addrinfo* serverinfo;
   struct addrinfo hints;

   sprintf(tmp, "%d", port);

   // (1) open socket

   memset(&hints, 0, sizeof(hints));
   hints.ai_family   = AF_UNSPEC;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_protocol = IPPROTO_TCP;

   res = getaddrinfo(host, tmp, &hints, &serverinfo);

   if (res)
   {
      fprintf(stderr, "Error: Failed to resolve host '%s' (%d / %s)\n",
            host ? host : "", errno, strerror(errno));
      return fail;
   }

   rsock= socket(serverinfo->ai_family, serverinfo->ai_socktype, serverinfo->ai_protocol);

   if (rsock < 0)
   {
      fprintf(stderr, "Error: Failed to create socket to host '%s' (%d / %s)\n",
            host ? host : "", errno, strerror(errno));

      freeaddrinfo(serverinfo);
      return fail;
   }

   if (::connect(rsock, serverinfo->ai_addr, serverinfo->ai_addrlen) != 0)
   {
      fprintf(stderr, "Error: Failed to connect to host '%s' (%d / %s)\n",
            host ? host : "", errno, strerror(errno));

      disconnect();
      freeaddrinfo(serverinfo);
      return fail;
   }

   freeaddrinfo(serverinfo);

   // (2) authentication

   if ((res= authenticate(pass)) != success)
      fprintf(stderr, "Error: Authentication at host '%s' failed! (Wrong password?)\n", host);

   return res;
}

//***************************************************************************
// disconnect
//***************************************************************************

int RConChannel::disconnect()
{
   if (rsock != na)
      ::close(rsock);

   rsock= na;

   return done;
}

//***************************************************************************
// send
//***************************************************************************

int RConChannel::send(int id, int cmd, const char* commandString)
{
   int res= 0;
   int commandLen= strlen(commandString);

   // packet size: id + cmd + commandString + null byte

   thePacket.clear();
   thePacket.size = sizeof(int) * 2 + commandLen + 2;
   thePacket.id = id;
   thePacket.cmd = cmd;

   if (thePacket.resize(thePacket.size) != success)
   {
      fprintf(stderr, "Error: Failed to resize buffer to (%d), can't receive packet!\n", thePacket.size);
      return fail;
   }

   sprintf(thePacket.data, "%.*s", commandLen, commandString);

   if ((res= _send((char*)&thePacket.size, sizeof(int))) != success)
      return fail;
   
   if ((res= _send((char*)&thePacket.id, sizeof(int))) != success)
      return fail;
   
   if ((res= _send((char*)&thePacket.cmd, sizeof(int))) != success)
      return fail;

   if ((res= _send(thePacket.data, thePacket.size - 2*sizeof(int))) != success)
      return fail;

   return success;
}

//***************************************************************************
// _send
//***************************************************************************

int RConChannel::_send(char* buffer, int len)
{
   int res= success;
   int bytesSent= 0;

   while (res >= 0 && (bytesSent < len))
   {
      res= ::send(rsock, buffer + bytesSent, len-bytesSent, 0);

      if (res < 0)
      {
         fprintf(stderr, "Error: Failed to send bytes (%d / %s)\n", errno, strerror(errno));
         return fail;
      }

      bytesSent+= res;
   }

   return bytesSent == len ? success : fail;

}

//***************************************************************************
// receive
//***************************************************************************

int RConChannel::receive()
{
   int res= success;

   thePacket.clear();

   if ((res= _receive((char*)&thePacket.size, thePacket.getSize(), sizeof(int))) != success)
      return res;

   if (thePacket.resize(thePacket.size) != success)
   {
      fprintf(stderr, "Error: Failed to resize buffer to (%d), can't receive packet!\n", thePacket.size);
      flushLine();
   }

   if (thePacket.size < 10) 
   {
      fprintf(stderr, "Error: invalid packet size (%d). Must over 10.\n", thePacket.size);
      flushLine();
      return fail;
   }

   if ((res= _receive((char*)&thePacket.id, thePacket.getSize(), sizeof(int))) != success)
      return res;
   
   if ((res= _receive((char*)&thePacket.cmd, thePacket.getSize(), sizeof(int))) != success)
      return res;

   if ((res= _receive(thePacket.data, thePacket.getSize(), thePacket.size - 2*sizeof(int))) != success)
      return res;

   return success;
}

//***************************************************************************
// _receive
//***************************************************************************

int RConChannel::_receive(char* buffer, int bufSize, int len)
{
   int res= success;
   int bytesRead= 0;

   while (res >= 0 && (len == na || (bytesRead < len)))
   {
      res= ::recv(rsock, len == na ? buffer : buffer+bytesRead, len == na ? bufSize : len-bytesRead, 0);

      if (res < 0)
      {
         fprintf(stderr, "Error: Failed to send bytes (%d / %s)\n", errno, strerror(errno));
         return fail;
      }

      bytesRead+= res;
   }

   return len == na ? success : (bytesRead == len ? success : fail);
}

//***************************************************************************
// flushLine
//***************************************************************************

int RConChannel::flushLine()
{
   _receive(thePacket.data, thePacket.getSize());
   return done;
}

//***************************************************************************
// authenticate
//***************************************************************************

int RConChannel::authenticate(const char *passwd)
{
   int res = send(RC_PID, RC_AUTHENTICATE, passwd);

   if (res) 
      return res;

   if ((res= receive()))
      return res;

   return thePacket.id == -1 ? fail : success;
}

//***************************************************************************
// rcon_command
//***************************************************************************

int RConChannel::sendCommand(const char* command)
{
   int res= success;

   res= send(RC_PID, RC_COMMAND, command);

   if (res)
      return res;

   if ((res= receive()))
      return res;

   if (thePacket.id != RC_PID)
      return errWrongSequence;

   // got response?

   if (thePacket.size <= 10)
      return wrnNoResponse;

   // clear trailing spaces and linefeed

   char* t= thePacket.data + strlen(thePacket.data);

   while (!*t && (t-thePacket.data))
      t--;

   while ((*t == ' ' || *t == '\n' || *t == '\r') && (t-thePacket.data))
      *t--= 0;

   if (!strcmp(thePacket.data, "Server received, But no response!!"))
      return wrnNoResponse;

   return success;
}

//***************************************************************************
// class RConPacket
//***************************************************************************
// ctor/dtor
//***************************************************************************

RConPacket::RConPacket()
{
   packetSize= 0;
   data= 0;

   resize(BUFFSIZE_DEF);
   clear();
}

RConPacket::~RConPacket()
{
   ::free((void*)data);
}

//***************************************************************************
// resize
//***************************************************************************

int RConPacket::resize(int newSize)
{
   if (newSize > BUFFSIZE_MAX)
      return fail;

   if (packetSize >= newSize)
      return done;

   data= (char*)realloc(data, newSize * sizeof(char));
   packetSize= newSize;

   return done;
}

//***************************************************************************
// clear
//***************************************************************************

void RConPacket::clear()
{
   size= id= cmd= 0;
   memset(data, 0, packetSize);
}

