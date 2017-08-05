//***************************************************************************
// File clusterchat.hpp
// Date 23.07.17 - #1
// Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
// --------------------------------------------------------------------------
// Ark ClusterChat / ClusterChat class
//***************************************************************************

#include <stdio.h>

#include "clusterchat.hpp"

//***************************************************************************
// class ClusterChat
//***************************************************************************
// ctor/dtor
//***************************************************************************

ClusterChat::ClusterChat()
{
}

ClusterChat::~ClusterChat()
{
   for (std::list<RConThread*>::iterator it= threads.begin(); it != threads.end(); ++it)
      delete *it;
}

//***************************************************************************
// init
//***************************************************************************

int ClusterChat::init(std::list<ServerConfig*>* configs)
{
   int res= success;

   for (std::list<ServerConfig*>::iterator it= configs->begin(); it != configs->end(); ++it)
   {
      RConThread* thread = new RConThread(&threads);
      ServerConfig* cfg= *it;

      res= thread->start(120, cfg->host.c_str(), cfg->port, cfg->password.c_str(), cfg->title.c_str());

      if (res)
      {
         thread->stop();
         delete thread;
         shutdown();

         return res;
      }

      threads.push_back(thread);

   }

   return res;
}

//***************************************************************************
// shutdown
//***************************************************************************

int ClusterChat::shutdown()
{
   printf("ClusterChat: Stopping worker threads ...\n");

   for (std::list<RConThread*>::iterator it= threads.begin(); it != threads.end(); ++it)
      (*it)->stop();

   return done;
}



