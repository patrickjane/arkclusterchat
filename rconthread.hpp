//***************************************************************************
// File rconthread.hpp
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / RCON thread
//***************************************************************************

#ifndef __RCONTHREAD_HPP__
#define __RCONTHREAD_HPP__

#include <list>                   // std::list
#include <string>                 // std::string
#include "thread.hpp"

class RConChannel;

//***************************************************************************
// struct Work, class WorkList
//***************************************************************************

struct Work
{
   std::string message;
   std::string server;
};

class WorkList
{
   friend class RConThread;

   public:
      
      int enqueue(Work* work) 
      {
         int res= 0;

         mutex.lock();
         list.push_back(work);
         res= (int)list.size();
         mutex.unlock();

         return res;
      }

      Work* dequeue()
      {
         Work* res= 0;
   
         mutex.lock();
         std::list<Work*>::iterator it= list.begin();
   
         if (it != list.end())
         {
            res= *it;
            list.pop_front();
         }

         mutex.unlock();
         return res;
      }

      size_t getCount() { return list.size(); }
   
   private:
      
      Mutex mutex;
      std::list<Work*> list;
};

//***************************************************************************
// class RConThread
//***************************************************************************

class RConThread : public Thread
{
   public:
      
      RConThread(std::list<RConThread*>* threads);
      virtual ~RConThread();

      // functions
      
      void wakeUp();
      void enqueue(Work* work) { queue.enqueue(work); wakeUp(); }

      int start(int blockTimeout, const char* aHostName, int aPport, const char* aPasswd, const char* aMap);
      int stop() { return Thread::stop(&waitCond); }

   protected:

      // frame
      
      int run();

      int init();
      int exit();
      
      int read();
      int write(Work* work);
      int command(const char* command);

      // functions

      int control();
      void tell(const char* format, ...);
      void error(const char* format, ...);
      
      void resizeBuffer(int newSize);

      // data

      Mutex waitMutex;
      CondVar waitCond;
      WorkList queue;       // write: other thread  read: rconthread

      char* hostName;
      char* passwd;
      char* tellBuffer;
      char* map;
      char* sendBuffer;
      int sendBufferSize;
      int port;
      RConChannel* channel;
      
      std::list<RConThread*>* threads;
};

//-----------------------------------------------------------------
#endif  // __RCONTHREAD_HPP__

