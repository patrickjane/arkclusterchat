//***************************************************************************
// File rconthread.cc
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / RCON thread
//***************************************************************************

#include "rconthread.hpp"
#include "channel.hpp"
#include <stdarg.h>

//#include <string>
#include <stdlib.h>

//***************************************************************************
// class RConThread
//***************************************************************************
//***************************************************************************
// constructors
//***************************************************************************

RConThread::RConThread(std::list<RConThread*>* aThreads)
{
   threads= aThreads;
   hostName= 0;
   passwd= 0;
   map= 0;
   port= -1;
   tellBuffer= (char*)calloc(1024*1024, sizeof(char));
   channel= new RConChannel;

   sendBuffer= (char*)calloc(1024*10, sizeof(char));
   sendBufferSize= 1024*10;
}

RConThread::~RConThread()
{
   ::free((void*)hostName);
   ::free((void*)passwd);
   ::free((void*)map);
   ::free((void*)tellBuffer);
   ::free((void*)sendBuffer);
   delete channel;
}

//***************************************************************************
// open
//***************************************************************************

int RConThread::start(int blockTimeout, const char* aHostName, int aPort, const char* aPasswd, const char* aMap)
{
   ::free((void*)hostName);
   ::free((void*)passwd);
   ::free((void*)map);

   hostName= strdup(aHostName);
   passwd= strdup(aPasswd);
   map= strdup(aMap);
   port= aPort;

   return Thread::start(blockTimeout);
}

//***************************************************************************
// init
//***************************************************************************

int RConThread::init()
{
   int res= channel->connect(hostName, port, passwd);

   if (!res)
      tell("Connected to host %s:%d", hostName, port);
   else
      error("Error: Failed to connect to host %s:%d (%d)", hostName, port, res);

   return res;
}

//***************************************************************************
// exit
//***************************************************************************

int RConThread::exit()
{
   channel->disconnect();
   return done;
}

//***************************************************************************
// run
//***************************************************************************

int RConThread::run()
{
   waitMutex.lock();

   while (!isState(isExit))
   {
      if (threads->size() > 1)
      {
         control();
         read();
      }

      if (!isState(isExit))
         waitCond.timedWaitMs(waitMutex, 1000);
   }

   waitMutex.unlock();

   tell("Shutting down");

   return 0;
}

//***************************************************************************
// Wake Up
//***************************************************************************

void RConThread::wakeUp()
{
   waitMutex.lock();
   waitCond.broadcast();
   waitMutex.unlock();
}

//***************************************************************************
// control
//***************************************************************************

int RConThread::control()
{
   // process 'work' ...

   Work* work= queue.dequeue();

   while (work)
   {
      write(work);

      delete work;

      work= queue.dequeue();
   }

   return done;
}

//***************************************************************************
// command
//***************************************************************************

int RConThread::command(const char* cmd)
{
   int res= channel->sendCommand(cmd);

   if (res == wrnNoResponse)
      ;//tell("No chat messages available");
   else if (res)
   {
      error("Error: Failed to send command (%d), reopening channel ...", res);

      exit();
      init();
   }

   return res;
}

//***************************************************************************
// read
//***************************************************************************

int RConThread::read()
{
   int res= command("GetChat");

   if (!res && strlen(channel->getBuffer()))
   {
      char *p1, *p2;
      p2= channel->getBuffer();

      while (true)
      {
         p1= strchr(p2, '\n');

         if (p1)
            *p1= 0;

         if (strncmp(p2, "SERVER: ", 8))
         {
            int nQueued= 0;

            if (Globals::cfgVerbose)
               tell("-> [%s]", p2);

            for (std::list<RConThread*>::iterator it= threads->begin(); it != threads->end(); ++it)
            {
               if (!Globals::cfgDebug && *it == this)
                  continue;

               if (Globals::cfgDebug && *it != this)
                  continue;

               Work* w= new Work;
               w->server.assign(map);
               w->message.assign(p2);

               if (*it == this)
                  queue.list.push_back(w);
               else
                  (*it)->enqueue(w);

               nQueued++;
            }
         }

         p2= p1+1;

         if (!p1 || !*p2)
            break;
      }
   }

   return res;
}
 
//***************************************************************************
// write
//***************************************************************************

int RConThread::write(Work* work)
{
   int res= success;

   resizeBuffer(work->message.length() + strlen(work->server.c_str()) + 30);
   snprintf(sendBuffer, sendBufferSize-1, "ServerChat [%s] %s", work->server.c_str(), work->message.c_str());

   res= command(sendBuffer);

   if (Globals::cfgVerbose)
      tell("<- [%s]", sendBuffer);

   return res;
}

//***************************************************************************
// tell
//***************************************************************************

void RConThread::tell(const char* format, ...)
{
   va_list args;
   va_start(args, format);

   vsprintf (tellBuffer, format, args);

   va_end (args);

   printf("[%s] %s\n", map, tellBuffer);
}

//***************************************************************************
// error
//***************************************************************************

void RConThread::error(const char* format, ...)
{
   va_list args;
   va_start(args, format);

   vsprintf (tellBuffer, format, args);

   va_end (args);

   fprintf(stderr, "[%s] %s\n", map, tellBuffer);
}

//***************************************************************************
// resizeBuffer
//***************************************************************************

void RConThread::resizeBuffer(int newSize)
{
   if (sendBufferSize >= newSize)
      return;

   sendBuffer= (char*)realloc(sendBuffer, newSize * sizeof(char));
   sendBufferSize= newSize;
}


