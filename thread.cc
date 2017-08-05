//***************************************************************************
// File thread.cc
// Date 24.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / Thread class
//***************************************************************************

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>

#include "thread.hpp"

//***************************************************************************
// Absolut Time
//***************************************************************************

static void absTime(timespec* abstime, int milli)
{
   timeval now;
   gettimeofday(&now, 0);

   unsigned long long usec = now.tv_usec + (milli % 1000) * 1000;

   abstime->tv_nsec = (usec % 1000000) * 1000;
   abstime->tv_sec  = now.tv_sec + (milli / 1000) + (usec / 1000000);
}

//***************************************************************************
// Mutex
//***************************************************************************

Mutex::Mutex()
{
   locked = 0;
   pthread_mutexattr_t attr;
   pthread_mutexattr_init(&attr);
   pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
   pthread_mutex_init(&mutex, &attr);
}

Mutex::~Mutex()
{
   pthread_mutex_destroy(&mutex);
}

void Mutex::lock()
{
   pthread_mutex_lock(&mutex);
   locked++;
}

int Mutex::tryLock()
{
   if (pthread_mutex_trylock(&mutex) == EBUSY)
      return fail;
   else
      locked++;

   return success;
}

void Mutex::unlock()
{
   locked--;

   if (locked <= 0)
   {
      locked = 0;
      pthread_mutex_unlock(&mutex);
   }
}

//***************************************************************************
// CondVar
//***************************************************************************

CondVar::CondVar()
{
   pthread_cond_init(&cond, 0);
}

CondVar::~CondVar()
{
   pthread_cond_broadcast(&cond);
   pthread_cond_destroy(&cond);
}

void CondVar::wait(Mutex& mutex)
{
   if (!mutex.locked)
      return;

   int locked = mutex.locked;
   mutex.locked = 0;
   pthread_cond_wait(&cond, &mutex.mutex);
   mutex.locked = locked;
}

int CondVar::timedWait(Mutex& mutex, int timeout)
{
   return timedWaitMs(mutex, timeout * 1000);
}

int CondVar::timedWaitMs(Mutex& mutex, int timeoutMs)
{
   int r = yes;

   // yes - condition signaled
   // no  - timeout

   if (mutex.locked)
   {
      timespec abstime;

      absTime(&abstime, timeoutMs);

      int locked = mutex.locked;
      mutex.locked = 0;

      if (pthread_cond_timedwait(&cond, &mutex.mutex, &abstime) == ETIMEDOUT)
         r = no;

      mutex.locked = locked;
   }

   return r;
}

void CondVar::broadcast()
{
   pthread_cond_broadcast(&cond);
}

//***************************************************************************
// Class Thread - Thread base class
//***************************************************************************
//***************************************************************************
// Ctor/Dtor
//***************************************************************************

Thread::Thread()
{
   exitStatus = 0;
   childTid   = 0;
   state      = isUnknown;
   joined     = no;

   pthread_attr_init(&attr);
}

Thread::~Thread()
{
   // join thread in case it was not joined yet.

   stop();

   pthread_attr_destroy(&attr);
}

//***************************************************************************
// Start
//***************************************************************************

int Thread::start(int blockTimeout)
{
   int res = success;

   if (isState(isRunning))
      return errAlreadyRunning;

   if (!joined && childTid)
      pthread_join(childTid, 0);

   if (blockTimeout != na)
   {
      // mutex is now locked by parent thread!

      controlMutex.lock();
   }

   setState(isRunning);
   joined = no;

   res = pthread_create(&childTid, 0, (void*(*)(void*))&startThread, (void *)this);

   if (res != success)
   {
      printf("Error: Can't start thread, errno (%d) - '%s'!\n", errno, strerror(errno));
      setState(isStopped);
      return res;
   }
   

   if (blockTimeout != na)
      return waitForStarted(blockTimeout);

   return success;
}

//***************************************************************************
// Start Thread
//***************************************************************************

void* Thread::startThread(Thread* thread)
{
   thread->setState(isRunning);
   thread->exitStatus = success;
   thread->exitStatus += thread->init();

   // probably wake up mainthread, waiting in ::waitForStarted()

   thread->controlMutex.lock();
   thread->controlCondVar.broadcast();
   thread->controlMutex.unlock();

   // start mainloop, if no problems occurred

   if (!thread->exitStatus)
      thread->exitStatus += thread->run();

   thread->exitStatus += thread->exit();
   thread->setState(isStopped);

   return 0;
}

//***************************************************************************
// wait for started
//***************************************************************************

int Thread::waitForStarted(int timeout)
{
   // sleep on condVar. will timeout or be broadcasted by thread

   controlCondVar.timedWaitMs(controlMutex, timeout * 1000);
   controlMutex.unlock();

   return exitStatus;
}

//***************************************************************************
// Stop
//***************************************************************************

int Thread::stop(CondVar* condVar)
{
   int res= success;

   setState(isExit);

   if (condVar)
      condVar->broadcast();

   if (childTid)
      res= pthread_join(childTid, 0);

   childTid = 0;
   setState(isExit);
   joined= yes;

   return res;
}
