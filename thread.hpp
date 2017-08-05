//***************************************************************************
// File thread.hpp
// Date 24.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / Thread class
//***************************************************************************

#ifndef THREAD_HPP
#define THREAD_HPP

#include <pthread.h>
#include "def.h"

//***************************************************************************
// Class Mutex
//***************************************************************************

class Mutex
{
   friend class CondVar;

   public:

      Mutex();
      ~Mutex();

      void lock();
      void unlock();

      int isLocked()     { return locked > 0; }
      int getLockCount() { return locked; }
      int tryLock();

   private:

      pthread_mutex_t mutex;
      int locked;
};

//***************************************************************************
// CondVar
//***************************************************************************

class CondVar
{
   public:

      CondVar();
      ~CondVar();

      void wait(Mutex& mutex);
      int timedWait(Mutex& mutex, int timeout);
      int timedWaitMs(Mutex& mutex, int timeoutMs);
      void broadcast();

   private:

      pthread_cond_t cond;
};

//***************************************************************************
// Thread
//***************************************************************************

class Thread
{
   public:

      enum Errors
      {
         errAlreadyRunning = -99
      };

      // object

      Thread();
      virtual ~Thread();

      // interface

      virtual int start(int blockTimeout = na);
      virtual int stop(CondVar* condVar = 0);

      // tests

      int isState(State aState) { return state == aState; }
      int sameThread() { return childTid == pthread_self(); }

   protected:

      void setState(State aState) { state = aState; }

      // functions

      virtual int init() { return done; }
      virtual int exit() { return done; }
      virtual int run() = 0;             // threads main run loop
      virtual int waitForStarted(int timeout);
      
      static void* startThread(Thread* thread);

      // data

      int joined;
      int exitStatus;

      Mutex controlMutex;
      CondVar controlCondVar;
      State state;

      pthread_t childTid;
      pthread_attr_t attr;
};

#endif // THREAD_HPP
